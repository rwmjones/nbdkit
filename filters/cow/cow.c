/* nbdkit
 * Copyright Red Hat
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
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "isaligned.h"
#include "ispowerof2.h"
#include "minmax.h"
#include "rounding.h"
#include "vector.h"

#include "cow.h"
#include "blk.h"

/* Read-modify-write requests are serialized through this global lock.
 * This is only used for unaligned requests which should be
 * infrequent.
 */
static pthread_mutex_t rmw_lock = PTHREAD_MUTEX_INITIALIZER;

unsigned blksize = 65536;       /* block size */

static bool cow_on_cache;

/* Cache on read ("cow-on-read") mode. */
extern enum cor_mode {
  COR_OFF,
  COR_ON,
  COR_PATH,
} cor_mode;
enum cor_mode cor_mode = COR_OFF;
const char *cor_path;

/* Mapping of exportname to overlay. */
static pthread_mutex_t blk_list_lock = PTHREAD_MUTEX_INITIALIZER;
struct export_mapping {
  char *exportname;
  struct blk_overlay *blk;
};
DEFINE_VECTOR_TYPE (blk_list, struct export_mapping);
static blk_list blks;

static void
cow_load (void)
{
  blk_load ();
}

static void
cow_unload (void)
{
  size_t i;

  /* Free up the overlays, bitmaps, close the backing files. */
  for (i = 0; i < blks.len; ++i) {
    nbdkit_debug ("cow: freeing overlay for export \"%s\"",
                  blks.ptr[i].exportname);
    free (blks.ptr[i].exportname);
    blk_free (blks.ptr[i].blk);
  }
  blk_list_reset (&blks);

  blk_unload ();
}

static int
cow_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "cow-block-size") == 0) {
    int64_t r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    if (r < 4096 || r > UINT_MAX || !is_power_of_2 (r)) {
      nbdkit_error ("cow-block-size is out of range (4096..2G) "
                    "or not a power of 2");
      return -1;
    }
    blksize = r;
    return 0;
  }
  else if (strcmp (key, "cow-on-cache") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    cow_on_cache = r;
    return 0;
  }
  else if (strcmp (key, "cow-on-read") == 0) {
    if (value[0] == '/') {
      cor_path = value;
      cor_mode = COR_PATH;
    }
    else {
      int r = nbdkit_parse_bool (value);
      if (r == -1)
        return -1;
      cor_mode = r ? COR_ON : COR_OFF;
    }
    return 0;
  }
  else {
    return next (nxdata, key, value);
  }
}

#define cow_config_help \
  "cow-block-size=<N>       Set COW block size.\n" \
  "cow-on-cache=<BOOL>      Copy cache (prefetch) requests to the overlay.\n" \
  "cow-on-read=<BOOL>|/PATH Copy read requests to the overlay."

/* Decide if cow-on-read is currently on or off. */
static bool
cow_on_read (void)
{
  switch (cor_mode) {
  case COR_ON: return true;
  case COR_OFF: return false;
  case COR_PATH: return access (cor_path, F_OK) == 0;
  default: abort ();
  }
}

struct handle {
  struct blk_overlay *blk;
};

static void *
cow_open (nbdkit_next_open *next, nbdkit_context *nxdata,
          int readonly, const char *exportname, int is_tls)
{
  struct handle *h;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  /* Find or create the overlay corresponding to this export. */
  {
    size_t i;

    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk_list_lock);
    for (i = 0; i < blks.len; ++i) {
      if (strcmp (blks.ptr[i].exportname, exportname) == 0) {
        nbdkit_debug ("cow: reusing existing overlay for export \"%s\"",
                      exportname);
        h->blk = blks.ptr[i].blk;
        break;
      }
    }
    if (!h->blk) {
      struct export_mapping new_entry;

      new_entry.exportname = strdup (exportname);
      if (new_entry.exportname == NULL) {
        nbdkit_error ("strdup: %m");
        free (h);
        return NULL;
      }
      h->blk = new_entry.blk = blk_create ();
      if (new_entry.blk == NULL) {
        free (new_entry.exportname);
        free (h);
        return NULL;
      }
      if (blk_list_append (&blks, new_entry) == -1) {
        free (new_entry.exportname);
        blk_free (new_entry.blk);
        free (h);
        return NULL;
      }
      nbdkit_debug ("cow: allocated new overlay for export \"%s\"",
                    exportname);
    }
  }

  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1, exportname) == -1) {
    /* Don't free h->blk here.  It is freed in cow_unload. */
    free (h);
    return NULL;
  }

  return h;
}

static void
cow_close (void *handle)
{
  struct handle *h = handle;

  /* Don't free h->blk here.  It is freed in cow_unload. */
  free (h);
}

/* Get the file size, set the cache size. */
static int64_t
cow_get_size (nbdkit_next *next,
              void *handle)
{
  struct handle *h = handle;
  int64_t size;
  int r;

  size = next->get_size (next);
  if (size == -1)
    return -1;

  nbdkit_debug ("cow: underlying file size: %" PRIi64, size);

  r = blk_set_size (h->blk, size);
  if (r == -1)
    return -1;

  return size;
}

/* Block size constraints. */
static int
cow_block_size (nbdkit_next *next, void *handle,
                uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  if (next->block_size (next, minimum, preferred, maximum) == -1)
    return -1;

  if (*minimum == 0) {         /* No constraints set by the plugin. */
    *minimum = 1;
    *preferred = blksize;
    *maximum = 0xffffffff;
  }
  else {
    if (*maximum >= blksize)
      *preferred = MAX (*preferred, blksize);
  }

  return 0;
}

/* Force an early call to cow_get_size because we have to set the
 * backing file size and bitmap size before any other read or write
 * calls.
 */
static int
cow_prepare (nbdkit_next *next,
             void *handle, int readonly)
{
  int64_t r;

  r = cow_get_size (next, handle);
  return r >= 0 ? 0 : -1;
}

static int
cow_can_write (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_trim (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_extents (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_flush (nbdkit_next *next, void *handle)
{
  return 1;
}

static int
cow_can_fua (nbdkit_next *next, void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int
cow_can_cache (nbdkit_next *next, void *handle)
{
  /* Cache next->can_cache now, so later calls to next->cache
   * don't fail, even though we override the answer here.
   */
  int r = next->can_cache (next);
  if (r == -1)
    return -1;
  return NBDKIT_CACHE_NATIVE;
}

static int
cow_can_multi_conn (nbdkit_next *next,
                    void *handle)
{
  /* Our cache is consistent between connections.  */
  return 1;
}

/* Override the plugin's .can_fast_zero, because our .zero is not fast */
static int
cow_can_fast_zero (nbdkit_next *next,
                   void *handle)
{
  /* It is better to advertise support even when we always reject fast
   * zero attempts.
   */
  return 1;
}

static int cow_flush (nbdkit_next *next, void *handle, uint32_t flags,
                      int *err);

/* Read data. */
static int
cow_pread (nbdkit_next *next,
           void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs, nrblocks;
  int r;

  if (!IS_ALIGNED (count | offset, blksize)) {
    block = malloc (blksize);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    assert (block);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r == -1)
      return -1;

    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  nrblocks = count / blksize;
  if (nrblocks > 0) {
    r = blk_read_multiple (h->blk, next,
                           blknum, nrblocks, buf, cow_on_read (), err);
    if (r == -1)
      return -1;

    buf += nrblocks * blksize;
    count -= nrblocks * blksize;
    offset += nrblocks * blksize;
    blknum += nrblocks;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r == -1)
      return -1;

    memcpy (buf, block, count);
  }

  return 0;
}

/* Write data. */
static int
cow_pwrite (nbdkit_next *next,
            void *handle, const void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  struct handle *h = handle;
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  if (!IS_ALIGNED (count | offset, blksize)) {
    block = malloc (blksize);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the rmw_lock over the whole operation.
     */
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memcpy (&block[blkoffs], buf, n);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;

    buf += n;
    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= blksize) {
    r = blk_write (h->blk, blknum, buf, err);
    if (r == -1)
      return -1;

    buf += blksize;
    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    assert (block);
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memcpy (block, buf, count);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

/* Zero data. */
static int
cow_zero (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  /* We are purposefully avoiding next->zero, so a zero request is
   * never faster than plain writes.
   */
  if (flags & NBDKIT_FLAG_FAST_ZERO) {
    *err = ENOTSUP;
    return -1;
  }

  block = malloc (blksize);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the rmw_lock over the whole operation.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  if (count >= blksize)
    memset (block, 0, blksize);
  while (count >= blksize) {
    /* XXX There is the possibility of optimizing this: since this loop is
     * writing a whole, aligned block, we should use FALLOC_FL_ZERO_RANGE.
     */
    r = blk_write (h->blk, blknum, block, err);
    if (r == -1)
      return -1;

    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memset (block, 0, count);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

/* Trim data. */
static int
cow_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  struct handle *h = handle;
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;

  if (!IS_ALIGNED (count | offset, blksize)) {
    block = malloc (blksize);
    if (block == NULL) {
      *err = errno;
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  if (blkoffs) {
    uint64_t n = MIN (blksize - blkoffs, count);

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;

    count -= n;
    offset += n;
    blknum++;
  }

  /* Aligned body */
  while (count >= blksize) {
    r = blk_trim (h->blk, blknum, err);
    if (r == -1)
      return -1;

    count -= blksize;
    offset += blksize;
    blknum++;
  }

  /* Unaligned tail */
  if (count) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&rmw_lock);
    r = blk_read (h->blk, next, blknum, block, cow_on_read (), err);
    if (r != -1) {
      memset (block, 0, count);
      r = blk_write (h->blk, blknum, block, err);
    }
    if (r == -1)
      return -1;
  }

  /* flags & NBDKIT_FLAG_FUA is deliberately ignored. */

  return 0;
}

static int
cow_flush (nbdkit_next *next, void *handle,
           uint32_t flags, int *err)
{
  /* Deliberately ignored. */
  return 0;
}

static int
cow_cache (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  struct handle *h = handle;
  CLEANUP_FREE uint8_t *block = NULL;
  uint64_t blknum, blkoffs;
  int r;
  uint64_t remaining = count; /* Rounding out could exceed 32 bits */
  enum cache_mode mode;

  switch (next->can_cache (next)) {
  case NBDKIT_CACHE_NONE:
    mode = BLK_CACHE_IGNORE;
    break;
  case NBDKIT_CACHE_EMULATE:
    mode = BLK_CACHE_READ;
    break;
  case NBDKIT_CACHE_NATIVE:
    mode = BLK_CACHE_PASSTHROUGH;
    break;
  default:
    abort ();                 /* Guaranteed thanks to early caching */
  }
  if (cow_on_cache)
    mode = BLK_CACHE_COW;

  assert (!flags);
  block = malloc (blksize);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  blknum = offset / blksize;  /* block number */
  blkoffs = offset % blksize; /* offset within the block */

  /* Unaligned head */
  remaining += blkoffs;
  offset -= blkoffs;

  /* Unaligned tail */
  remaining = ROUND_UP (remaining, blksize);

  /* Aligned body */
  while (remaining) {
    r = blk_cache (h->blk, next, blknum, block, mode, err);
    if (r == -1)
      return -1;

    remaining -= blksize;
    offset += blksize;
    blknum++;
  }

  return 0;
}

/* Extents. */
static int
cow_extents (nbdkit_next *next,
             void *handle, uint32_t count32, uint64_t offset, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  struct handle *h = handle;
  const bool can_extents = next->can_extents (next);
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  uint64_t count = count32;
  uint64_t end;
  uint64_t blknum;

  /* To make this easier, align the requested extents to whole blocks.
   * Note that count is a 64 bit variable containing at most a 32 bit
   * value so rounding up is safe here.
   */
  end = offset + count;
  offset = ROUND_DOWN (offset, blksize);
  end = ROUND_UP (end, blksize);
  count = end - offset;
  blknum = offset / blksize;

  assert (IS_ALIGNED (offset, blksize));
  assert (IS_ALIGNED (count, blksize));
  assert (count > 0);           /* We must make forward progress. */

  while (count > 0) {
    bool present, trimmed;
    struct nbdkit_extent e;

    blk_status (h->blk, blknum, &present, &trimmed);

    /* Present in the overlay. */
    if (present) {
      e.offset = offset;
      e.length = blksize;

      if (trimmed)
        e.type = NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO;
      else
        e.type = 0;

      if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
        *err = errno;
        return -1;
      }

      blknum++;
      offset += blksize;
      count -= blksize;
    }

    /* Not present in the overlay, but we can ask the plugin. */
    else if (can_extents) {
      uint64_t range_offset = offset;
      uint32_t range_count = 0;
      size_t i;
      int64_t size;

      /* Asking the plugin for a single block of extents is not
       * efficient for some plugins (eg. VDDK) so ask for as much data
       * as we can.
       */
      for (;;) {
        /* nbdkit_extents_full cannot read more than a 32 bit range
         * (range_count), but count is a 64 bit quantity, so don't
         * overflow range_count here.
         */
        if (range_count >= UINT32_MAX - blksize + 1) break;

        blknum++;
        offset += blksize;
        count -= blksize;
        range_count += blksize;

        if (count == 0) break;
        blk_status (h->blk, blknum, &present, &trimmed);
        if (present) break;
      }

      /* Don't ask for extent data beyond the end of the plugin. */
      size = next->get_size (next);
      if (size == -1)
        return -1;

      if (range_offset + range_count > size) {
        unsigned tail = range_offset + range_count - size;
        range_count -= tail;
      }

      CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 =
        nbdkit_extents_full (next, range_count, range_offset, flags, err);
      if (extents2 == NULL)
        return -1;

      for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
        e = nbdkit_get_extent (extents2, i);
        if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
          *err = errno;
          return -1;
        }
      }
    }

    /* Otherwise assume the block is non-sparse. */
    else {
      e.offset = offset;
      e.length = blksize;
      e.type = 0;

      if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
        *err = errno;
        return -1;
      }

      blknum++;
      offset += blksize;
      count -= blksize;
    }

    /* If the caller only wanted the first extent, and we've managed
     * to add at least one extent to the list, then we can drop out
     * now.  (Note calling nbdkit_add_extent above does not mean the
     * extent got added since it might be before the first offset.)
     */
    if (req_one && nbdkit_extents_count (extents) > 0)
      break;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "cow",
  .longname          = "nbdkit copy-on-write (COW) filter",
  .load              = cow_load,
  .unload            = cow_unload,
  .open              = cow_open,
  .close             = cow_close,
  .config            = cow_config,
  .config_help       = cow_config_help,
  .prepare           = cow_prepare,
  .get_size          = cow_get_size,
  .block_size        = cow_block_size,
  .can_write         = cow_can_write,
  .can_flush         = cow_can_flush,
  .can_trim          = cow_can_trim,
  .can_extents       = cow_can_extents,
  .can_fua           = cow_can_fua,
  .can_cache         = cow_can_cache,
  .can_fast_zero     = cow_can_fast_zero,
  .can_multi_conn    = cow_can_multi_conn,
  .pread             = cow_pread,
  .pwrite            = cow_pwrite,
  .zero              = cow_zero,
  .trim              = cow_trim,
  .flush             = cow_flush,
  .cache             = cow_cache,
  .extents           = cow_extents,
};

NBDKIT_REGISTER_FILTER (filter)
