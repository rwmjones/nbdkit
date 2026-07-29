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
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "minmax.h"
#include "regions.h"
#include "vector.h"

int64_t map_size = -1; /* map-size, -1 means use underlying plugin size */

/* Range definition.
 *
 * A single struct range stores a range from start-end (inclusive).
 * end can be INT64_MAX to indicate the end of the file.
 *
 * 'range_list' stores the list of ranges, unsorted.
 */
struct range {
  uint64_t start, end;
  uint64_t dest;                /* mapping in underlying plugin */
  const char *description;      /* link to the command line parameter */
  int prio;                     /* priority, higher = earlier in command line */
};
DEFINE_VECTOR_TYPE (ranges, struct range);
static ranges range_list;

/* region_list covers the whole address space with ranges, with no
 * overlaps or gaps.
 */
static regions region_list;

static void
map_unload (void)
{
  ranges_reset (&range_list);
  regions_reset (&region_list);
}

/* Parse "START-END:DEST" into a range.  Adds the range to range_list,
 * or exits with an error.
 */
static void
parse_range (const char *value)
{
  static int prio = INT_MAX;
  struct range range = { .description = value, .prio = prio-- };

  if (sscanf (value, "%" SCNu64 "-%" SCNu64 ":%" SCNu64,
              &range.start, &range.end, &range.dest) != 3) {
    nbdkit_error ("cannot parse range: %s", range.description);
    exit (EXIT_FAILURE);
  }

  if (range.end < range.start) {
    nbdkit_error ("invalid range, end < start: %s", range.description);
    exit (EXIT_FAILURE);
  }

  /* Note that range.end == range.start is a 1 byte range.  This means
   * that every range has > 0 length.
   */

  if (ranges_append (&range_list, range) == -1) {
    nbdkit_error ("realloc: %m");
    exit (EXIT_FAILURE);
  }
}

static int
map_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  int r;

  if (strcmp (key, "map") == 0) {
    parse_range (value);
    return 0;
  }
  else if (strcmp (key, "map-size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    map_size = r;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

/* Insert a 1-1 mapping range with lowest priority. */
static void
add_implicit_range (void)
{
  struct range implicit_range = {
    .description = "implicit 1-1 mapping",
    .start = 0,
    .end = INT64_MAX,
    .dest = 0,
    .prio = INT_MIN,
  };

  if (ranges_append (&range_list, implicit_range) == -1) {
    nbdkit_error ("realloc: %m");
    exit (EXIT_FAILURE);
  }
}

DEFINE_VECTOR_TYPE (boundaries, uint64_t);

static int
compare_boundaries (const uint64_t *i1, const uint64_t *i2)
{
  if (*i1 < *i2)
    return -1;
  else if (*i1 > *i2)
    return 1;
  else
    return 0;
}

static int
compare_ranges (const struct range *r1, const struct range *r2)
{
  if (r1->start < r2->start)
    return -1;
  else if (r1->start > r2->start)
    return 1;
  else
    return 0;
}

/* Use -D map.ranges=1 to debug ranges and regions in detail. */
NBDKIT_DLL_PUBLIC int map_debug_ranges = 0;

/* Convert the overlapping ranges to a non-overlapping region list. */
static void
convert_to_regions (void)
{
  size_t i, j;
  const size_t orig_range_list_len = range_list.len;
  boundaries bounds = empty_vector;

  /* First ensure every range is split up at the boundary of every
   * overlapping range.  This adds lots of new ranges to the list, but
   * in the end there will be no ranges that partially overlap.
   */

  /* Find every boundary, build a list, sort and unique it. */
  for (i = 0; i < range_list.len; ++i) {
    if (boundaries_append (&bounds, range_list.ptr[i].start) == -1 ||
        boundaries_append (&bounds, range_list.ptr[i].end+1) == -1) {
      nbdkit_error ("realloc: %m");
      exit (EXIT_FAILURE);
    }
  }
  boundaries_sort (&bounds, compare_boundaries);
  boundaries_uniq (&bounds, compare_boundaries);

  if (map_debug_ranges) {
    nbdkit_debug ("finding boundaries:");
    for (i = 0; i < bounds.len; ++i)
      nbdkit_debug ("    bounds[%zu] = %" PRIu64, i, bounds.ptr[i]);
    nbdkit_debug ("ranges before splitting:");
    for (i = 0; i < range_list.len; ++i)
      nbdkit_debug ("    range[%zu] = { start=%" PRIu64 ", end=%" PRIu64
                    ", from=%s }",
                    i, range_list.ptr[i].start, range_list.ptr[i].end,
                    range_list.ptr[i].description);
  }

  /* Split every original range at any bounds that overlap it. */
  for (i = 0; i < orig_range_list_len; ++i) {
    const uint64_t i_start = range_list.ptr[i].start;
    const uint64_t i_end = range_list.ptr[i].end;
    ranges new_range_list = empty_vector;

    /* XXX We could binary search here since the boundaries are sorted. */
    for (j = 0; j < bounds.len; ++j) {
      uint64_t b = bounds.ptr[j];

      /* b <= i_end here because the bounds split before each offset,
       * so if there is a bound at 'end' then we would want to split
       * just before offset 'end' to create 1 byte range.
       */
      if (i_start < b && b <= i_end) {
        struct range new_range = range_list.ptr[i];
        new_range.start = b;
        new_range.dest += b - i_start;
        if (ranges_append (&new_range_list, new_range) == -1) {
          nbdkit_error ("realloc: %m");
          exit (EXIT_FAILURE);
        }
      }
    }

    /* Did we split range[i]? */
    if (new_range_list.len > 0) {
      /* Shorten range[i] so it ends at the first new range. */
      range_list.ptr[i].end = new_range_list.ptr[0].start - 1;
      /* Shorten each new range so it ends at the next new range. */
      for (j = 0; j < new_range_list.len - 1; ++j)
        new_range_list.ptr[j].end = new_range_list.ptr[j+1].start - 1;
      /* Append the new ranges to the end of the original list. */
      if (ranges_append_array (&range_list,
                               new_range_list.ptr, new_range_list.len) == -1) {
        nbdkit_error ("realloc: %m");
        exit (EXIT_FAILURE);
      }
    }

    ranges_reset (&new_range_list);
  }

  /* Don't need this any longer. */
  boundaries_reset (&bounds);

  if (map_debug_ranges) {
    nbdkit_debug ("ranges after splitting:");
    for (i = 0; i < range_list.len; ++i)
      nbdkit_debug ("    range[%zu] = { start=%" PRIu64 ", end=%" PRIu64
                    ", from=%s }",
                    i, range_list.ptr[i].start, range_list.ptr[i].end,
                    range_list.ptr[i].description);
  }

  /* Sort the ranges by start offset. */
  ranges_sort (&range_list, compare_ranges);

  /* Check there are no more partially overlapping ranges. */
  for (i = 0; i < range_list.len - 1; ++i) {
    if (range_list.ptr[i].start == range_list.ptr[i+1].start)
      assert (range_list.ptr[i].end == range_list.ptr[i+1].end);
    else
      assert (range_list.ptr[i].end < range_list.ptr[i+1].start);
  }

  /* Now remove all lower priority ranges that have a higher priority
   * range at the same position.
   */
  for (i = 0; i < range_list.len - 1; ++i) {
    if (range_list.ptr[i].start == range_list.ptr[i+1].start) {
      if (range_list.ptr[i].prio < range_list.ptr[i+1].prio) {
        ranges_remove (&range_list, i);
        i--;
      }
      else {
        ranges_remove (&range_list, i+1);
        i--;
      }
    }
  }

  if (map_debug_ranges) {
    nbdkit_debug ("after removing lower priority ranges:");
    for (i = 0; i < range_list.len; ++i)
      nbdkit_debug ("    range[%zu] = { start=%" PRIu64 ", end=%" PRIu64
                    ", from=%s }",
                    i, range_list.ptr[i].start, range_list.ptr[i].end,
                    range_list.ptr[i].description);
  }

  /* Now there should be no overlapping ranges and no gaps. */
  assert (range_list.len > 0);
  assert (range_list.ptr[0].start == 0);

  for (i = 0; i < range_list.len - 1; ++i)
    assert (range_list.ptr[i].end + 1 == range_list.ptr[i+1].start);

  /* Finally we can convert the ranges to regions. */
  for (i = 0; i < range_list.len; ++i) {
    const struct range range = range_list.ptr[i];

    assert (virtual_size (&region_list) == range.start);
    if (append_region_end (&region_list, range.description, range.end,
                           0, 0, region_file, i) == -1) {
      nbdkit_error ("append region: %m");
      exit (EXIT_FAILURE);
    }

    nbdkit_debug ("map: [%" PRIu64 "-%" PRIu64 "] -> %" PRIu64 " (from: %s)",
                  range.start, range.end, range.dest,
                  range.description);
  }
}

static int
map_config_complete (nbdkit_next_config_complete *next, nbdkit_backend *nxdata)
{
  add_implicit_range ();
  convert_to_regions ();
  return next (nxdata);
}

#define map_config_help \
  "map=<START>-<END>:<DEST>   Map START-END to DEST.\n" \
  "map-size=<SIZE>            Set size to SIZE."

static int64_t
map_get_size (nbdkit_next *next, void *handle)
{
  /* If map-size is set, use that, otherwise use plugin size. */
  if (map_size >= 0)
    return map_size;
  else
    return next->get_size (next);
}

/* This higher order function performs the mapping for each operation. */
static int
do_mapping (const char *op_name,
            void *private,
            int (*op) (void *private, uint32_t len, uint64_t offset,
                       uint64_t original_offset),
            nbdkit_next *next, uint32_t count, uint64_t offset, int *err)
{
  int64_t plugin_size;

  plugin_size = next->get_size (next);
  if (plugin_size == -1)
    return -1;

  while (count > 0) {
    const struct region *region;
    const struct range *range;
    uint64_t ofs, len;
    size_t i;

    region = find_region (&region_list, offset);
    assert (region != NULL);
    assert (region->type == region_file);
    assert (region->start <= offset);
    ofs = offset - region->start;
    len = MIN (region->end - offset + 1, (uint64_t) count);
    assert (len > 0);
    i = region->u.i;
    assert (i < range_list.len);
    range = &range_list.ptr[i];

    /* Check the range lies within the plugin. */
    if (range->dest + ofs + len > plugin_size) {
      nbdkit_error ("%s: I/O access beyond end of plugin (from rule: %s)",
                    op_name, range->description);
      *err = EIO;
      return -1;
    }

    if (op (private, (uint32_t) len, range->dest+ofs, offset) == -1)
      return -1;

    count -= len;
    offset += len;
  }

  return 0;
}

/* Read data. */
struct pread_data {
  nbdkit_next *next;
  void *handle;
  void *buf;
  uint32_t flags;
  int *err;
};

static int
op_pread (void *private, uint32_t len, uint64_t offset, uint64_t unused)
{
  struct pread_data *pread_data = private;

  if (pread_data->next->pread (pread_data->next, pread_data->buf,
                               len, offset,
                               pread_data->flags, pread_data->err) == -1)
    return -1;

  pread_data->buf += len;
  return 0;
}

static int
map_pread (nbdkit_next *next,
           void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  struct pread_data pread_data = {
    .next = next, .handle = handle, .buf = buf, .flags = flags, .err = err
  };

  return do_mapping ("pread", &pread_data, op_pread,
                     next, count, offset, err);
}

/* Write data. */
struct pwrite_data {
  nbdkit_next *next;
  void *handle;
  const void *buf;
  uint32_t flags;
  int *err;
};

static int
op_pwrite (void *private, uint32_t len, uint64_t offset, uint64_t unused)
{
  struct pwrite_data *pwrite_data = private;

  if (pwrite_data->next->pwrite (pwrite_data->next, pwrite_data->buf,
                                 len, offset,
                                 pwrite_data->flags, pwrite_data->err) == -1)
    return -1;

  pwrite_data->buf += len;
  return 0;
}

static int
map_pwrite (nbdkit_next *next,
            void *handle,
            const void *buf, uint32_t count, uint64_t offset, uint32_t flags,
            int *err)
{
  struct pwrite_data pwrite_data = {
    .next = next, .handle = handle, .buf = buf, .flags = flags, .err = err
  };

  return do_mapping ("pwrite", &pwrite_data, op_pwrite,
                     next, count, offset, err);
}

/* Trim data. */
struct trim_data {
  nbdkit_next *next;
  void *handle;
  uint32_t flags;
  int *err;
};

static int
op_trim (void *private, uint32_t len, uint64_t offset, uint64_t unused)
{
  struct trim_data *trim_data = private;

  return trim_data->next->trim (trim_data->next,
                                len, offset,
                                trim_data->flags, trim_data->err);
}

static int
map_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  struct trim_data trim_data = {
    .next = next, .handle = handle, .flags = flags, .err = err
  };

  return do_mapping ("trim", &trim_data, op_trim,
                     next, count, offset, err);
}

/* Zero data. */
struct zero_data {
  nbdkit_next *next;
  void *handle;
  uint32_t flags;
  int *err;
};

static int
op_zero (void *private, uint32_t len, uint64_t offset, uint64_t unused)
{
  struct zero_data *zero_data = private;

  return zero_data->next->zero (zero_data->next,
                                len, offset,
                                zero_data->flags, zero_data->err);
}

static int
map_zero (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  struct zero_data zero_data = {
    .next = next, .handle = handle, .flags = flags, .err = err
  };

  return do_mapping ("zero", &zero_data, op_zero,
                     next, count, offset, err);
}

/* Cache data. */
struct cache_data {
  nbdkit_next *next;
  void *handle;
  uint32_t flags;
  int *err;
};

static int
op_cache (void *private, uint32_t len, uint64_t offset, uint64_t unused)
{
  struct cache_data *cache_data = private;

  return cache_data->next->cache (cache_data->next,
                                  len, offset,
                                  cache_data->flags, cache_data->err);
}

static int
map_cache (nbdkit_next *next,
           void *handle, uint32_t count, uint64_t offset, uint32_t flags,
           int *err)
{
  struct cache_data cache_data = {
    .next = next, .handle = handle, .flags = flags, .err = err
  };

  return do_mapping ("cache", &cache_data, op_cache,
                     next, count, offset, err);
}

/* Extents. */
struct extents_data {
  nbdkit_next *next;
  void *handle;
  uint32_t flags;
  int *err;
  struct nbdkit_extents *extents;
};

static int
op_extents (void *private, uint32_t len, uint64_t offset,
            uint64_t original_offset)
{
  struct extents_data *extents_data = private;
  CLEANUP_EXTENTS_FREE struct nbdkit_extents *extents2 = NULL;
  size_t i;
  struct nbdkit_extent e;
  const int64_t end = offset + len;

  extents2 = nbdkit_extents_new (offset, end);
  if (extents2 == NULL) {
    *extents_data->err = errno;
    return -1;
  }

  if (extents_data->next->extents (extents_data->next, len, offset,
                                   extents_data->flags, extents2,
                                   extents_data->err) == -1)
    return -1;

  for (i = 0; i < nbdkit_extents_count (extents2); ++i) {
    e = nbdkit_get_extent (extents2, i);
    e.offset += original_offset - offset;
    if (nbdkit_add_extent (extents_data->extents,
                           e.offset, e.length, e.type) == -1) {
      *extents_data->err = errno;
      return -1;
    }
  }

  return 0;
}

static int
map_extents (nbdkit_next *next,
             void *handle, uint32_t count, uint64_t offset, uint32_t flags,
             struct nbdkit_extents *extents, int *err)
{
  struct extents_data extents_data = {
    .next = next, .handle = handle, .flags = flags, .err = err,
    .extents = extents
  };

  return do_mapping ("extents", &extents_data, op_extents,
                     next, count, offset, err);
}

static struct nbdkit_filter filter = {
  .name              = "map",
  .longname          = "nbdkit map filter",
  .unload            = map_unload,
  .config            = map_config,
  .config_complete   = map_config_complete,
  .config_help       = map_config_help,
  .get_size          = map_get_size,
  .pread             = map_pread,
  .pwrite            = map_pwrite,
  .trim              = map_trim,
  .zero              = map_zero,
  .extents           = map_extents,
  .cache             = map_cache,
};

NBDKIT_REGISTER_FILTER(filter)
