/* nbdkit
 * Copyright (C) 2018 Red Hat Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include <nbdkit-filter.h>

#include "avltree.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Storage of the map and efficient lookups:
 *
 * The map read from the map file is stored in an AVL tree (a
 * self-balancing binary search tree) to allow efficient and fast
 * lookups.
 *
 * Normally we might use an interval tree for this, but interval trees
 * are more complex to implement and anyway we can use the AVL tree as
 * long as we make a single simplifying assumption: intervals cannot
 * overlap.
 *
 * When building the tree we first write the mappings to a flat array
 * and sort by the base address.  Then we split any overlapping
 * intervals so there are no overlaps before inserting them into the
 * final tree.
 *
 * The result of this is a simple AVL tree sorted by base address
 * which allows a simple, fast lookup of client addresses back to
 * addresses in the underlying plugin.
 */

struct entry {
  /* The start and end (inclusive) of the region after being mapped.
   * out_end == UINT64_MAX to mean "to end of input".
   */
  uint64_t out_start, out_end;

  /* The region this maps to in the plugin.  end/length is implied. */
  uint64_t in_start;

  /* The line in the original file that this mapping came from.
   * This is used both for debugging and for disambiguation because
   * later lines in the file map on top of earlier lines.
   */
  int lineno;
};

/* Compare entries. */
static int
entry_compare (const void *ev1, const void *ev2)
{
  const struct entry *e1 = ev1;
  const struct entry *e2 = ev2;

  if (e1->out_start < e2->out_start)
    return -1;
  else if (e1->out_start > e2->out_start)
    return 1;
  else
    return 0;
}

/* Tree. */
struct tree {
  struct avltree avltree;
  struct entry entry;
};

/* Compare tree nodes. */
static int
tree_compare (const void *nv1, const void *nv2)
{
  const struct tree *node1 = nv1;
  const struct tree *node2 = nv2;

  return entry_compare (&node1->entry, &node2->entry);
}

/* Return the rightmost node in a tree. */
static const struct tree *
rightmost (const struct tree *root)
{
  if (root == NULL)
    return NULL;
  return rightmost ((struct tree *) root->avltree.right);
}

static char *filename;          /* Map file. */
static struct tree *root;       /* Parsed interval tree. */

/* Parse the map file into the flat array of intervals. */
static int
parse_map_file_to_array (const char *filename,
                         struct entry **flat, size_t *flat_len)
{
  /* Set of whitespace in the map file. */
  static const char whitespace[] = " \t\n\r";

  FILE *fp;
  ssize_t r;
  size_t len = 0;
  char *line = NULL;
  int lineno = 0;

  fp = fopen (filename, "r");
  if (fp == NULL) {
    nbdkit_error ("open: %s: %m", filename);
    return -1;
  }
  while ((r = getline (&line, &len, fp)) != -1) {
    char *p, *q, *saveptr;
    size_t n;
    int64_t i;
    int64_t in_length; /* signed because -1 means end of input */
    struct entry entry;

    lineno++;
    entry.lineno = lineno;

    /* Remove anything after # (comment) character. */
    p = strchr (line, '#');
    if (p)
      *p = '\0';

    /* Trim whitespace at beginning of the line. */
    n = strspn (line, whitespace);
    if (n > 0)
      memmove (line, &line[n], strlen (&line[n]));

    /* Trim whitespace at end of the line (including \n and \r). */
    n = strlen (line);
    while (n > 0) {
      if (strspn (&line[n-1], whitespace) == 0)
        break;
      line[n-1] = '\0';
      n--;
    }

    /* Ignore blank lines. */
    if (n == 0)
      continue;

    /* First field.
     * Expecting: "start,length" or "start-end" or "start-" or "start".
     */
    p = strtok_r (line, whitespace, &saveptr);
    if (p == NULL) {
      /* AFAIK this can never happen. */
      nbdkit_error ("%s:%d: could not read token", filename, lineno);
      goto err;
    }
    if ((q = strchr (p, ',')) != NULL) { /* start,length */
      *q = '\0'; q++;
      i = nbdkit_parse_size (p);
      if (i == -1)
        goto err;
      entry.in_start = i;
      i = nbdkit_parse_size (q);
      if (i == -1)
        goto err;
      in_length = i;
    }
    else if ((q = strchr (p, '-')) != NULL) { /* start-end or start- */
      *q = '\0'; q++;
      i = nbdkit_parse_size (p);
      if (i == -1)
        goto err;
      entry.in_start = i;
      if (*q == '\0')
        in_length = -1;
      else {
        i = nbdkit_parse_size (q);
        if (i == -1)
          goto err;
        if (i < entry.in_start) {
          nbdkit_error ("%s:%d: end < start", filename, lineno);
          goto err;
        }
        in_length = i - entry.in_start + 1;
      }
    }
    else {                      /* start */
      i = nbdkit_parse_size (p);
      if (i == -1)
        goto err;
      entry.in_start = i;
      in_length = -1;
    }

    /* A zero-length mapping isn't an error, but can be ignored immediately. */
    if (in_length == 0)
      continue;

    /* Second field.  Expecting a single offset. */
    p = strtok_r (NULL, whitespace, &saveptr);
    i = nbdkit_parse_size (p);
    if (i == -1)
      goto err;
    entry.out_start = i;

    /* Calculate the end of the output region. */
    if (in_length != -1)
      entry.out_end = entry.out_start + in_length - 1;
    else
      entry.out_end = UINT64_MAX; /* To end of input. */

    /* We just ignore everything on the line after the second field.
     * But don't put anything there, we might use this for something
     * in future.
     */

    nbdkit_debug ("%s:%d: "
                  "in.start=%" PRIu64 ", in.length=%" PRIi64 ", "
                  "out.start=%" PRIu64 ", out.end=%" PRIu64,
                  filename, lineno,
                  entry.in_start, in_length,
                  entry.out_start, entry.out_end);

    /* Allocate a new entry in the array. */
    *flat = realloc (*flat, (*flat_len + 1) * sizeof (struct entry));
    if (*flat == NULL) {
      nbdkit_error ("realloc");
      goto err;
    }
    (*flat)[*flat_len] = entry;
    (*flat_len)++;
  }

  fclose (fp);
  free (line);
  return 0;

 err:
  fclose (fp);
  free (line);
  return -1;
}

static int
load_map_file (const char *filename)
{
  struct entry *flat = NULL;
  size_t flat_len = 0;
  size_t i;
  struct tree *node;

  /* Allocate a new node or call error.  Note we don't need to free
   * these nodes because afterwards they are owned by the tree.
   */
#define ALLOC_NODE                              \
  do {                                          \
    node = malloc (sizeof *node);               \
    if (node == NULL) {                         \
      nbdkit_error ("malloc: %m");              \
      goto err;                                 \
    }                                           \
  } while (0)
  /* Insert a fully constructed node in the tree. */
#define INSERT_NODE                                                     \
  do {                                                                  \
    nbdkit_debug ("tree: inserting [%" PRIu64 ", %" PRIu64 "] (line %d)", \
                  node->entry.out_start, node->entry.out_end,           \
                  node->entry.lineno);                                  \
    root = avltree_insert (root, node, tree_compare);                   \
  } while (0)

  if (parse_map_file_to_array (filename, &flat, &flat_len) == -1)
    return -1;

  nbdkit_debug ("read %zu entries from %s", flat_len, filename);

  if (flat_len == 0) {
    nbdkit_error ("%s: no entries found", filename);
    return -1;
  }

  /* Sort the flat array by base address. */
  qsort (flat, flat_len, sizeof flat[0], entry_compare);

  /* Now iterate over the flat array and insert the final
   * entries in the tree.
   */
  for (i = 0; i < flat_len; ++i) {
    /* If it's the last in the list, or if this entry is completely
     * disjoint from the entry following, insert it.
     */
    if (i == flat_len-1 || flat[i].out_end < flat[i+1].out_start) {
      ALLOC_NODE;
      node->entry = flat[i];
      INSERT_NODE;
    }
    else {
      size_t j = i+1;
      uint64_t p = flat[i].out_start;

      /* This entry overlaps with the one (or more) entries following.
       * Now we iterate over the following overlapping entries.
       */
      while (j < flat_len && p < flat[i].out_end) {
        /* Is there a part of the i'th region before the j'th region?
         *
         * p
         * +----------------------------+
         * | flat[i]                    |
         * +---------------+-----------------------+
         *                 | flat[j]               |
         *                 +-----------------------+
         * \______________/
         *   this region
         */
        if (p < flat[j].out_start) {
          ALLOC_NODE;
          node->entry = flat[i];
          node->entry.out_start = p;
          node->entry.out_end = flat[j].out_start-1;
          p = node->entry.out_end+1;
          INSERT_NODE;
        }

        if (flat[i].out_end > flat[j].out_start) {
          /* Where the i'th region overlaps the j'th region.
           *
           *                 p
           * +----------------------------+
           * | flat[i]                    |
           * +---------------+-----------------------+
           *                 | flat[j]               |
           *                 +-----------------------+
           *                 \___________/
           *                  this region
           */
          ALLOC_NODE;
          /* Later lines in the file override earlier lines in the file. */
          if (flat[i].lineno > flat[j].lineno)
            node->entry = flat[i];
          else
            node->entry = flat[j];
          node->entry.out_start = p;
          if (flat[j].out_end <= flat[i].out_end) {
            node->entry.out_end = flat[j].out_end;
            /* We've consumed flat[j] completely. */
            memmove (&flat[j], &flat[j+1],
                     (flat_len-(j+1)) * sizeof flat[0]);
            flat_len--;
          }
          else {
            node->entry.out_end = flat[i].out_end;
            /* We've partially consumed flat[j]. */
            flat[j].out_start = flat[i].out_end+1; /* XXX -1? */
          }
          p = node->entry.out_end+1;
          INSERT_NODE;
        }

        j++;
      }
    }
  }

  free (flat);
  return 0;

 err:
  free (flat);
  return -1;
}

/* Look up an address in the tree.
 * Returns the entry or NULL if unmapped.
 *
 * Note this only works because intervals in the tree are guaranteed
 * not to overlap.  See description at top of file.
 */
static const struct entry *
lookup (const struct tree *root, uint64_t p)
{
  if (root == NULL)
    return NULL;
  else if (p < root->entry.out_start)
    return lookup ((struct tree *) root->avltree.left, p);
  else if (p > root->entry.out_end)
    return lookup ((struct tree *) root->avltree.right, p);
  else
    return &root->entry;
}

/* lookup_next_mapped is similar to lookup() above, but if the address
 * is unmapped it returns the next mapped entry.  It can still return
 * NULL if there are no more mappings after the address.
 */
static const struct entry *
lookup_next_mapped (const struct tree *root, uint64_t p)
{
  if (root == NULL)
    return NULL;
  else if (p < root->entry.out_start) {
    /* Get the rightmost mapping in the left subtree.  If it is
     * < p then the current root is the returned value.
     */
    const struct tree *left = rightmost ((struct tree *) root->avltree.left);
    if (left && left->entry.out_end < p)
      return &root->entry;
    else
      return lookup_next_mapped ((struct tree *) root->avltree.left, p);
  }
  else if (p > root->entry.out_end)
    return lookup_next_mapped ((struct tree *) root->avltree.right, p);
  else
    /* Address was mapped, so return the map entry. */
    return &root->entry;
}

/* Free the filename and tree on exit. */
static void
free_tree (struct tree *root)
{
  if (root == NULL)
    return;

  free_tree ((struct tree *) root->avltree.left);
  free_tree ((struct tree *) root->avltree.right);
  free (root);
}

static void
map_unload (void)
{
  free (filename);
  free_tree (root);
}

/* Expect map=filename on the command line, pass everything else
 * through.
 */
static int
map_config (nbdkit_next_config *next, void *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "map") == 0) {
    filename = nbdkit_realpath (value);
    if (filename == NULL)
      return -1;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

/* Check that map was supplied. */
static int
map_config_complete (nbdkit_next_config_complete *next, void *nxdata)
{
  if (filename == NULL) {
    nbdkit_error ("map=<filename> must be passed to the map filter");
    return -1;
  }

  if (load_map_file (filename) == -1)
    return -1;

  return next (nxdata);
}

#define map_config_help \
  "map=<FILENAME>      (required) Map file."

/* Get size. */
static int64_t
map_get_size (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  const struct tree *last = rightmost (root);

  if (!last)                    /* Probably can never happen. */
    return 0;
  else if (last->entry.out_end == UINT64_MAX) /* To end of input plugin. */
    return next_ops->get_size (nxdata);
  else
    return last->entry.out_end;
}

/* Read data. */
static int
map_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, void *buf, uint32_t count, uint64_t offs,
           uint32_t flags, int *err)
{
  const struct entry *this, *next;
  uint32_t c;

  while (count > 0) {
    /* This region (containing offs). */
    this = lookup (root, offs);
    if (this) {
      /* How much can we read from this mapping? */
      c = this->out_end - offs + 1;
      if (c > count)
        c = count;
      if (next_ops->pread (nxdata, buf, c,
                           offs + this->in_start - this->out_start,
                           0, err) == -1)
        return -1;
      buf += c;
      offs += c;
      count -= c;
    }
    else {
      /* Offset is unmapped.  Find the next mapped region, and
       * write zeroes to the buffer for the unmapped part.
       */
      next = lookup_next_mapped (root, offs);
      if (next) {
        c = next->out_start - offs + 1;
        if (c > count)
          c = count;
        memset (buf, 0, c);
        buf += c;
        offs += c;
        count -= c;
      }
      else {
        /* Unmapped to the end, just set the remaining buffer to zero. */
        memset (buf, 0, count);
        count = 0;
      }
    }
  }

  return 0;
}

/* Write data. */
static int
map_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle,
            const void *buf, uint32_t count, uint64_t offs, uint32_t flags,
            int *err)
{
  abort ();
}

/* Trim data. */
static int
map_trim (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  abort ();
}

/* Zero data. */
static int
map_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offs, uint32_t flags,
          int *err)
{
  abort ();
}

static struct nbdkit_filter filter = {
  .name              = "map",
  .longname          = "nbdkit map filter",
  .version           = PACKAGE_VERSION,
  .unload            = map_unload,
  .config            = map_config,
  .config_complete   = map_config_complete,
  .config_help       = map_config_help,
  .get_size          = map_get_size,
  .pread             = map_pread,
  .pwrite            = map_pwrite,
  .trim              = map_trim,
  .zero              = map_zero,
};

NBDKIT_REGISTER_FILTER(filter)
