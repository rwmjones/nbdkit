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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#else
/* Some old platforms lack atomic types, but 32 bit ints are usually
 * "atomic enough".
 */
#define _Atomic /**/
#endif

#ifdef HAVE_ZLIB_NG
#include <zlib-ng.h>
#define z_stream zng_stream
#define inflateInit2 zng_inflateInit2
#define inflate zng_inflate
#define inflateEnd zng_inflateEnd
#else
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif
#endif

#ifdef HAVE_LIBZSTD
#include <zstd.h>
#endif

#include <nbdkit-filter.h>

#include "byte-swapping.h"
#include "cleanup.h"
#include "isaligned.h"
#include "minmax.h"
#include "rounding.h"

#include "qcow2.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t virtual_size = -1;
static int64_t qcow2_size = -1;
static enum {
  COMPRESSION_NONE, COMPRESSION_DEFLATE, COMPRESSION_ZSTD,
} compression_type;
static struct qcow2_header header;
static uint64_t cluster_size;

/* L1 table read from the disk and byte swapped.  There are
 * header.l1_size entries in the array.
 */
static uint64_t *l1_table;

/* L2 tables loaded on demand.
 *
 * XXX Keep track of how much we have allocated and remove old table
 * entries if it gets too large.
 */
struct l2_table {
  pthread_mutex_t lock; /* lock must be held when accessing this table */
  time_t last_used;
  uint64_t *l2_entry; /* either NULL or array size is l2_entries */
} *l2_tables; /* array of structs size is header.l1_size */
static uint64_t l2_entries;
static unsigned l2_entries_bits;

static void
qcow2dec_unload (void)
{
  size_t i;

  if (l2_tables) {
    for (i = 0; i < header.l1_size; ++i) {
      pthread_mutex_destroy (&l2_tables[i].lock);
      free (l2_tables[i].l2_entry);
    }
    free (l2_tables);
  }
  free (l1_table);
}

/* Which compression do we support (in --dump-plugin output). */
static void
qcow2dec_dump_plugin (void)
{
#if defined(HAVE_ZLIB) || defined(HAVE_ZLIB_NG)
  printf ("qcow2dec_deflate=yes\n");
#endif
#ifdef HAVE_LIBZSTD
  printf ("qcow2dec_zstd=yes\n");
#endif
}

/* Force read-only. */
static int
qcow2dec_can_write (nbdkit_next *next,
                    void *handle)
{
  return 0;
}

static int
qcow2dec_can_cache (nbdkit_next *next,
                    void *handle)
{
  return NBDKIT_CACHE_EMULATE;
}

/* Because it is read-only, this filter is consistent across connections. */
static int
qcow2dec_can_multi_conn (nbdkit_next *next,
                         void *handle)
{
  return 1;
}

static int
qcow2dec_can_extents (nbdkit_next *next,
                      void *handle)
{
  return 1;
}

/* The first thread that calls .prepare reads the qcow2 metadata. */
static int get_qcow2_metadata (nbdkit_next *next);

static int
qcow2dec_prepare (nbdkit_next *next, void *handle, int readonly)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lock);

  if (virtual_size >= 0)
    return 0;
  else
    return get_qcow2_metadata (next);
}

static int
get_qcow2_metadata (nbdkit_next *next)
{
  size_t i;
  int64_t t;
  int err = 0;
  uint64_t incompatible_features;
  bool compressed;
  uint64_t l1_table_size;

  /* Get the qcow2 file size. */
  t = next->get_size (next);
  if (t == -1)
    return -1;

  /* It's unlikely to be a valid qcow2 file if it is smaller than
   * 128K.  Actually the smallest qcow2 file I could create was 192K.
   */
  if (t < 128*1024) {
    nbdkit_error ("plugin is too small to contain a qcow2 file");
    errno = EINVAL;
    return -1;
  }
  qcow2_size = t;

  /* Read the header and byte-swap it. */
  if (next->pread (next, &header, sizeof header, 0, 0, &err) == -1) {
    errno = err;
    return -1;
  }
  // header.magic doesn't need byte swapping
  header.version                 = be32toh (header.version);
  header.backing_file_offset     = be64toh (header.backing_file_offset);
  header.backing_file_size       = be32toh (header.backing_file_size);
  header.cluster_bits            = be32toh (header.cluster_bits);
  header.size                    = be64toh (header.size);
  header.crypt_method            = be32toh (header.crypt_method);
  header.l1_size                 = be32toh (header.l1_size);
  header.l1_table_offset         = be64toh (header.l1_table_offset);
  header.refcount_table_offset   = be64toh (header.refcount_table_offset);
  header.refcount_table_clusters = be64toh (header.refcount_table_clusters);
  header.nb_snapshots            = be32toh (header.nb_snapshots);
  header.snapshots_offset        = be64toh (header.snapshots_offset);
  header.incompatible_features   = be64toh (header.incompatible_features);
  header.compatible_features     = be64toh (header.compatible_features);
  header.autoclear_features      = be64toh (header.autoclear_features);
  header.refcount_order          = be32toh (header.refcount_order);
  header.header_length           = be32toh (header.header_length);
  // header.compression_type is a single byte

  if (memcmp (&header.magic, QCOW2_MAGIC_STRING,
              strlen (QCOW2_MAGIC_STRING)) != 0) {
    nbdkit_error ("plugin does not contain a valid qcow2 file");
    errno = EINVAL;
    return -1;
  }

  if (header.version < 2 || header.version > 3) {
    nbdkit_error ("plugin contains qcow2 file sub-version %" PRIu32 ", "
                  "and we only support versions 2 or 3",
                  header.version);
    errno = EINVAL;
    return -1;
  }

  if (header.backing_file_offset != 0) {
    nbdkit_error ("plugin contains qcow2 with a backing file "
                  "which is not supported");
    errno = EINVAL;
    return -1;
  }

  cluster_size = UINT64_C(1) << header.cluster_bits;
  if (header.cluster_bits < 9 || header.cluster_bits > 21) {
    nbdkit_error ("plugin contains qcow2 with a cluster size of "
                  "%" PRIu64 " (1 << %" PRIu32 " bits) "
                  "which is not supported",
                  cluster_size, header.cluster_bits);
    errno = EINVAL;
    return -1;
  }

  if (header.crypt_method != 0) {
    nbdkit_error ("plugin contains encrypted qcow2 "
                  "which is not supported");
    errno = EINVAL;
    return -1;
  }

  if (header.nb_snapshots != 0) {
    nbdkit_error ("plugin contains qcow2 with internal snapshots "
                  "which is not supported");
    errno = EINVAL;
    return -1;
  }

  /* If the file version is 2, fill in the version 3 fields with
   * defaults to make this easier.
   */
  if (header.version == 2) {
    header.incompatible_features = 0;
    header.compatible_features = 0;
    header.autoclear_features = 0;
    header.refcount_order = 4;
    header.header_length = 72;
  }

  if ((header.version > 2 && header.header_length < 104)
      || header.header_length >= 512) {
    nbdkit_error ("plugin contains qcow2 with invalid header length");
    errno = EINVAL;
    return -1;
  }

  if (header.header_length < sizeof header) {
    uint8_t *p = (uint8_t *) &header;
    memset (p + header.header_length, 0, sizeof header - header.header_length);
  }

  incompatible_features = header.incompatible_features;
  if (incompatible_features & (1 << QCOW2_INCOMPAT_FEAT_COMPRESSION_TYPE_BIT)) {
    compressed = true;
    incompatible_features &= ~ (1 << QCOW2_INCOMPAT_FEAT_COMPRESSION_TYPE_BIT);
  }

  if (incompatible_features != 0) {
    nbdkit_error ("plugin contains qcow2 with unsupported extended features "
                  "(%" PRIu64 ")" /* XXX decode them using the table */,
                  header.incompatible_features);
    errno = ENOTSUP;
    return -1;
  }

  if (compressed) {
    switch (header.compression_type) {
    case 0: compression_type = COMPRESSION_DEFLATE; break;
    case 1: compression_type = COMPRESSION_ZSTD; break;
    default:
      nbdkit_error ("plugin contains qcow2 with unknown compression type (%d)",
                    (int) header.compression_type);
      errno = ENOTSUP;
      return -1;
    }
  }

  /* Allocate and load the L1 table.  As we have to load the whole L1
   * table into RAM, set some reasonable limits here.
   */
  if (header.l1_size > 1 << 28) /* We won't allocate more than 2G */ {
    nbdkit_error ("plugin contains qcow2 file with too large L1 table, "
                  "refusing to load it");
    errno = ERANGE;
    return -1;
  }
  l1_table_size = header.l1_size * 8;
  if (header.l1_table_offset < 512 || header.l1_table_offset >= header.size
      || header.l1_table_offset + l1_table_size > header.size) {
    nbdkit_error ("plugin contains qcow2 file with L1 table outside the file, "
                  "refusing to load it");
    errno = ERANGE;
    return -1;
  }
  l1_table = malloc (l1_table_size);
  if (l1_table == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }
  if (next->pread (next, l1_table, l1_table_size, header.l1_table_offset,
                   0, &err) == -1) {
    errno = err;
    free (l1_table);
    return -1;
  }
  /* Byte-swap the L1 table. */
  for (i = 0; i < header.l1_size; ++i)
    l1_table[i] = be64toh (l1_table[i]);

  /* We don't validate the L2 table pointers in the L1 table until we
   * start to read the file.  But we can calculate the number of
   * entries in an L2 table and allocate the top level array.
   */
  l2_entries = cluster_size / 8;
  l2_entries_bits = header.cluster_bits - 3;
  assert ((UINT64_C(1) << l2_entries_bits) == l2_entries);
  l2_tables = calloc (header.l1_size, sizeof (struct l2_table));
  if (l2_tables == NULL) {
    nbdkit_error ("malloc");
    free (l1_table);
    return -1;
  }
  for (i = 0; i < header.l1_size; ++i)
    pthread_mutex_init (&l2_tables[i].lock, NULL);

  /* Print some debug information about the file. */
  nbdkit_debug ("qcow2dec: QCOW2 (v%" PRIu32 ") file size %" PRIi64
                " virtual size %" PRIu64,
                header.version, qcow2_size, header.size);
  nbdkit_debug ("qcow2dec: cluster size %" PRIu64, cluster_size);
  nbdkit_debug ("qcow2dec: L1 entries %" PRIu32 " at file offset %" PRIu64,
                header.l1_size, header.l1_table_offset);
  nbdkit_debug ("qcow2dec: L2 entries per table %" PRIu64, l2_entries);
  nbdkit_debug ("qcow2dec: incompatible features %" PRIu64,
                header.incompatible_features);
  nbdkit_debug ("qcow2dec: compatible features %" PRIu64,
                header.compatible_features);
  nbdkit_debug ("qcow2dec: autoclear features %" PRIu64,
                header.autoclear_features);
  nbdkit_debug ("qcow2dec: header length %" PRIu32, header.header_length);
  switch (compression_type) {
  case COMPRESSION_NONE:
    nbdkit_debug ("qcow2dec: no compression"); break;
  case COMPRESSION_DEFLATE:
    nbdkit_debug ("qcow2dec: compression type deflate"); break;
  case COMPRESSION_ZSTD:
    nbdkit_debug ("qcow2dec: compression type zstd"); break;
  }

  /* Store the virtual size.  Do this last since virtual_size >= 0 is
   * a sentinel that we managed to open and decode the qcow2 header
   * and data structures.
   */
  virtual_size = header.size;

  return 0;
}

/* Get the virtual size. */
static int64_t
qcow2dec_get_size (nbdkit_next *next,
                   void *handle)
{
  int64_t t;

  /* This must be true because .prepare must have been called. */
  assert (virtual_size >= 0);

  /* Check the qcow2 size didn't change underneath us. */
  t = next->get_size (next);
  if (t == -1)
    return -1;
  if (t != qcow2_size) {
    nbdkit_error ("plugin size changed unexpectedly: "
                  "you must restart nbdkit so the qcow2 filter "
                  "can parse the file again");
    return -1;
  }

  return virtual_size;
}

/* Read data. */
static int read_cluster (nbdkit_next *next, void *buf, uint64_t offset,
                         uint32_t flags, int *err);
static int read_l2_entry (nbdkit_next *next, uint64_t offset, uint32_t flags,
                          bool *l2_present, uint64_t *l2_entry, int *err);
static int read_compressed_cluster (nbdkit_next *next, void *buf,
                                    uint64_t offset, uint64_t l2_entry,
                                    uint32_t flags, int *err);

static int
qcow2dec_pread (nbdkit_next *next,
                void *handle,
                void *buf,
                uint32_t count, uint64_t offset, uint32_t flags,
                int *err)
{
  CLEANUP_FREE uint8_t *cluster = NULL;
  uint64_t cloffs;

  if (!IS_ALIGNED (count | offset, cluster_size)) {
    cluster = malloc (cluster_size);
    if (cluster == NULL) {
      nbdkit_error ("malloc: %m");
      *err = errno;
      return -1;
    }
  }

  cloffs = offset % cluster_size; /* offset within the cluster */

  /* Unaligned head */
  if (cloffs) {
    uint64_t n = MIN (cluster_size - cloffs, count);

    if (read_cluster (next, cluster, offset & ~(cluster_size-1),
                      flags, err) == -1)
      return -1;
    memcpy (buf, &cluster[cloffs], n);

    buf += n;
    count -= n;
    offset += n;
  }

  /* Aligned body */
  while (count >= cluster_size) {
    if (read_cluster (next, buf, offset, flags, err) == -1)
      return -1;

    buf += cluster_size;
    count -= cluster_size;
    offset += cluster_size;
  }

  /* Unaligned tail */
  if (count) {
    if (read_cluster (next, cluster, offset, flags, err) == -1)
      return -1;
    memcpy (buf, cluster, count);
  }

  return 0;
}

/* Read the data in exactly one cluster.  'offset' must be aligned to
 * cluster_size.
 */
static int
read_cluster (nbdkit_next *next, void *buf, uint64_t offset,
              uint32_t flags, int *err)
{
  bool l2_present;
  uint64_t l2_entry;
  uint64_t file_offset;

  /* Get the L2 table entry. */
  if (read_l2_entry (next, offset, flags, &l2_present, &l2_entry, err) == -1)
    return -1;
  /* L2 table is unallocated, so return zeroes. */
  if (!l2_present) {
    memset (buf, 0, cluster_size);
    return 0;
  }
  if (l2_entry & QCOW2_L2_ENTRY_TYPE_MASK) /* 1 = compressed cluster. */
    return read_compressed_cluster (next, buf, offset, l2_entry, flags, err);

  /* From here on we know this is a standard cluster because we
   * handled compressed clusters above and we don't support extended
   * clusters.
   */
  if ((l2_entry & QCOW2_L2_ENTRY_RESERVED_MASK) != 0) {
    nbdkit_error ("invalid L2 table entry: "
                  "reserved bits are not zero (0x%" PRIx64 ")",
                  l2_entry);
    *err = ERANGE;
    return -1;
  }

  file_offset = l2_entry & QCOW2_L2_ENTRY_OFFSET_MASK;

  /* Does the cluster read as all zeroes?  Note we can check
   * file_offset == 0 here because we don't support external files.
   */
  if ((l2_entry & 1) != 0 || file_offset == 0) {
    memset (buf, 0, cluster_size);
    return 0;
  }

  if (file_offset < cluster_size
      || (file_offset & (cluster_size-1)) != 0
      || file_offset > qcow2_size - cluster_size) {
    nbdkit_error ("invalid L2 table entry: "
                  "offset of L2 table is beyond the end of the file");
    *err = ERANGE;
    return -1;
  }

  return next->pread (next, buf, cluster_size, file_offset, flags, err);
}

static int
read_l2_entry (nbdkit_next *next, uint64_t offset, uint32_t flags,
               bool *l2_present, uint64_t *l2_entry, int *err)
{
  size_t i;
  uint64_t l1_index, l2_index, l1_entry, l2_offset;
  // uint64_t l1_top_bit;
  uint64_t *l2_table;

  assert ((offset & (cluster_size - 1)) == 0);

  /* Get the L1 table entry. */
  l2_index = (offset / cluster_size) & (l2_entries - 1);
  l1_index = (offset / cluster_size) >> l2_entries_bits;
  assert (l1_index < header.l1_size);

  l1_entry = l1_table[l1_index];
  if ((l1_entry & QCOW2_L1_ENTRY_RESERVED_MASK) != 0) {
    nbdkit_error ("invalid L1 table entry at offset %" PRIu64
                  ": reserved bits are not zero",
                  l1_index);
    *err = ERANGE;
    return -1;
  }

  /* Get the offset of the L2 table. */
  l2_offset = l1_entry & QCOW2_L1_ENTRY_OFFSET_MASK;
  //l1_top_bit = l1_entry >> 63;

  /* L2 table is unallocated, so return zeroes. */
  if (l2_offset == 0) {
    *l2_present = false;
    return 0;
  }
  *l2_present = true;

  /* Read the L2 table cluster into memory. */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&l2_tables[l1_index].lock);

    time (&l2_tables[l1_index].last_used);

    l2_table = l2_tables[l1_index].l2_entry;
    if (l2_table == NULL) {

      if (l2_offset < cluster_size
          || (l2_offset & (cluster_size-1)) != 0
          || l2_offset > qcow2_size - cluster_size) {
        nbdkit_error ("invalid L1 table entry at offset %" PRIu64
                      ": offset of L2 table is beyond the end of the file",
                      l1_index);
        *err = ERANGE;
        return -1;
      }

      l2_table = malloc (cluster_size);
      if (l2_table == NULL) {
        nbdkit_error ("malloc: %m");
        *err = errno;
        return -1;
      }

      if (next->pread (next, l2_table, cluster_size, l2_offset,
                       flags, err) == -1)
        return -1;

      /* Byte-swap the L2 table. */
      for (i = 0; i < l2_entries; ++i)
        l2_table[i] = be64toh (l2_table[i]);

      /* Store it back in the global table so we won't reread it again. */
      l2_tables[l1_index].l2_entry = l2_table;
    }
  }

  /* Return the L2 table entry. */
  *l2_entry = l2_table[l2_index];
  return 0;
}

#if defined(HAVE_ZLIB) || defined(HAVE_ZLIB_NG)

static void
zerror (const char *op, const z_stream *strm, int zerr, int *err)
{
  if (zerr == Z_MEM_ERROR) {
    *err = errno = ENOMEM;
    nbdkit_error ("%s: %m", op);
  }
  else {
    *err = EIO;
    if (strm->msg)
      nbdkit_error ("%s: %s", op, strm->msg);
    else
      nbdkit_error ("%s: unknown error: %d", op, zerr);
  }
}

static int
inflate_compressed_cluster (void *buf,
                            const void *compressed_cluster,
                            size_t compressed_size,
                            uint64_t file_offset, /* for debugging */
                            int *err)
{
  z_stream strm;
  int zerr;

  memset (&strm, 0, sizeof strm);
  zerr = inflateInit2 (&strm, -12);
  if (zerr != Z_OK) {
    zerror ("inflateInit", &strm, zerr, err);
    return -1;
  }

  strm.next_in = (void *) compressed_cluster;
  strm.avail_in = compressed_size;
  strm.next_out = buf;
  strm.avail_out = cluster_size;

  zerr = inflate (&strm, Z_FINISH);
  /* Z_STREAM_END is the expected value.  Z_BUF_ERROR can happen
   * because we may begin to read the next compressed stream in the
   * last sector of the input.  Just ignore it (same as qemu).
   */
  if (zerr != Z_STREAM_END && zerr != Z_BUF_ERROR) {
    zerror ("inflate", &strm, zerr, err);
    return -1;
  }
  if (strm.avail_out != 0) {
    nbdkit_error ("deflate: incomplete compressed stream read "
                  "at qcow2 offset 0x%" PRIx64 ": "
                  "read %zu bytes of input, wrote %" PRIu64 " bytes of output",
                  file_offset,
                  compressed_size - strm.avail_in,
                  cluster_size - strm.avail_out);
    *err = EIO;
    return -1;
  }
  zerr = inflateEnd (&strm);
  if (zerr != Z_OK) {
    zerror ("inflateEnd", &strm, zerr, err);
    return -1;
  }

  return 0;
}

#endif /* HAVE_ZLIB || HAVE_ZLIB_NG */

#ifdef HAVE_LIBZSTD

static int
zstd_compressed_cluster (void *buf,
                         const void *compressed_cluster,
                         size_t compressed_size,
                         uint64_t file_offset, /* for debugging */
                         int *err)
{
  size_t r = 0;
  ZSTD_DCtx *ctx;
  ZSTD_outBuffer out = { .dst = buf, .size = cluster_size, .pos = 0 };
  ZSTD_inBuffer in =
    { .src = compressed_cluster, .size = cluster_size, .pos = 0 };

  ctx = ZSTD_createDCtx();
  if (ctx == NULL) {
    nbdkit_error ("ZSTD_createDCtx: %m");
    *err = ENOMEM;
    return -1;
  }

  while (out.pos < out.size) {
    size_t last_in_pos = in.pos;
    size_t last_out_pos = out.pos;
    r = ZSTD_decompressStream (ctx, &out, &in);

    if (ZSTD_isError (r)) {
      nbdkit_error ("zstd: error decompressing cluster "
                    "at qcow2 offset 0x%" PRIx64 " (compressed size %zu): "
                    "%s",
                    file_offset, compressed_size,
                    ZSTD_getErrorName (r));
      *err = EIO;
      ZSTD_freeDCtx (ctx);
      return -1;
    }

    /* See comment in qemu source about not getting stuck. */
    if (last_in_pos >= in.pos && last_out_pos >= out.pos) {
      nbdkit_error ("zstd: not making progress with decompression");
      *err = EIO;
      ZSTD_freeDCtx (ctx);
      return -1;
    }
  }

  ZSTD_freeDCtx (ctx);

  if (r > 0) {
    nbdkit_error ("zstd: incomplete compressed stream read "
                  "at qcow2 offset 0x%" PRIx64 ": "
                  "extra bytes %zu",
                  file_offset, r);
    *err = EIO;
    return -1;
  }

  return 0;
}

#endif /* HAVE_LIBZSTD */

static int
read_compressed_cluster (nbdkit_next *next, void *buf,
                         uint64_t offset, uint64_t l2_entry,
                         uint32_t flags, int *err)
{
  /* The qcow2 description doesn't explain 'x' very well, so:
   *
   * cluster_bits   cluster_size    x       l2_entry
   *      9           512          61       bits 0..55 = offs
   *                                        bits 56-60 = must be zero
   *                                        bit 61 = 1 or 2 sectors
   *     16         65536          54       bits 0..53 = offs
   *                                        bits 54..61 = #sectors-1
   *     21            2M          49       bits 0..49 = offs
   *                                        bits 42..61 = #sectors-1
   * for all x:
   *                                        bit 62 = 1 (compressed cluster)
   *                                        bit 63 = 0 (compressed cluster)
   */
  const int x = 62 - (header.cluster_bits - 8);
  const uint64_t offset_mask = (UINT64_C(1) << x) - 1;
  const uint64_t sector_mask = (UINT64_C(1) << (header.cluster_bits - 8)) - 1;
  uint64_t file_offset, nr_sectors, max_read, compressed_size;
  CLEANUP_FREE void *compressed_cluster = NULL;

  /* Get the host file offset. */
  file_offset = l2_entry & offset_mask;
  nr_sectors = 1 + ((l2_entry >> x) & sector_mask);

  if (file_offset & ~((UINT64_C(1) << 56) - 1)) {
    nbdkit_error ("invalid compressed L2 table entry: "
                  "reserved bits in offset are not zero (0x%" PRIx64 ")",
                  l2_entry);
    *err = ERANGE;
    return -1;
  }

  /* The compressed data does not necessary occupy the whole
   * nr_sectors.  This doesn't matter normally, where we read slightly
   * more than we need.  However it matters at the end of the qcow2
   * file where we mustn't read beyond the end.  Thus calculate the
   * actual compressed size here and adjust if we're at the end of the
   * file.
   */
  compressed_size = nr_sectors * 512;
  if (file_offset + compressed_size > qcow2_size)
    compressed_size = qcow2_size - file_offset;

  if (file_offset < 512
      || compressed_size > qcow2_size
      || file_offset + compressed_size > qcow2_size) {
    nbdkit_error ("invalid compressed L2 table entry: "
                  "file offset or number of sectors is out of range "
                  "(file_offset=0x%" PRIx64 ", "
                  "nr_sectors=0x%" PRIx64 ", "
                  "compressed_size=0x%" PRIx64 ", "
                  "l2_entry=0x%" PRIx64 ")",
                  file_offset, nr_sectors, compressed_size, l2_entry);
    *err = ERANGE;
    return -1;
  }

  /* Since for large cluster_sizes, nr_sectors can grow quite large
   * (eg. cluster_size = 2M, maximum nr_sectors = 1M + 1), limit what
   * we are prepared to allocate.  Note that qemu itself won't make a
   * compressed cluster which is larger than the original (it writes
   * an uncompressed cluster instead) so this is just an emergency
   * brake.
   */
  max_read = cluster_size * 2;
  if (compressed_size > max_read) {
    nbdkit_error ("invalid compressed L2 table entry: "
                  "compressed cluster is > %" PRIu64 " bytes",
                  max_read);
    *err = ENOMEM;
    return -1;
  }

  compressed_cluster = malloc (compressed_size);
  if (compressed_cluster == NULL) {
    nbdkit_error ("malloc: %m");
    *err = errno;
    return -1;
  }
  if (next->pread (next, compressed_cluster, compressed_size, file_offset,
                   flags, err) == -1)
    return -1;

  /* This is the time to find out if we support this type of
   * compression.  We don't do this when opening the file because not
   * all clusters are compressed and we may not ever read a compressed
   * cluster.  Only show the error once.
   */
  switch (compression_type) {
  case COMPRESSION_NONE: /* ? maybe for QCOW2 v2 */
  case COMPRESSION_DEFLATE:
#if defined(HAVE_ZLIB) || defined(HAVE_ZLIB_NG)
    return inflate_compressed_cluster (buf,
                                       compressed_cluster, compressed_size,
                                       file_offset, err);
#else
    {
      static _Atomic int zlib_show_error = 1;
      if (zlib_show_error) {
        nbdkit_error ("%s compression is not supported "
                      "by this build of nbdkit", "zlib");
        zlib_show_error = 0;
      }
      return -1;
    }
#endif

  case COMPRESSION_ZSTD:
#ifdef HAVE_LIBZSTD
    return zstd_compressed_cluster (buf,
                                    compressed_cluster, compressed_size,
                                    file_offset, err);
#else
    {
      static _Atomic int zstd_show_error = 1;
      if (zstd_show_error) {
        nbdkit_error ("%s compression is not supported "
                      "by this build of nbdkit", "zstd");
        zstd_show_error = 0;
      }
      return -1;
    }
#endif

  default:
    abort ();
  }
}

/* Extents. */
static int
qcow2dec_extents (nbdkit_next *next,
                  void *handle,
                  uint32_t count32, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents,
                  int *err)
{
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  uint64_t count = count32;
  uint64_t end;

  /* To make this easier, align the requested extents to whole
   * clusters.  Note that count is a 64 bit variable containing at
   * most a 32 bit value so rounding up is safe here.
   */
  end = offset + count;
  offset = ROUND_DOWN (offset, cluster_size);
  end = ROUND_UP (end, cluster_size);
  count = end - offset;

  assert (IS_ALIGNED (offset, cluster_size));
  assert (IS_ALIGNED (count, cluster_size));
  assert (count > 0);           /* We must make forward progress. */

  while (count > 0) {
    bool l2_present;
    uint64_t l2_entry;
    uint64_t file_offset;
    struct nbdkit_extent e = { .offset = offset, .length = cluster_size };

    /* Note: Don't pass flags here, since this is expecting pread
     * flags (always 0).
     */
    if (read_l2_entry (next, offset, 0, &l2_present, &l2_entry, err) == -1)
      return -1;

    /* L2 table is unallocated. */
    if (!l2_present)
      e.type = NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO;
    /* Compressed cluster, so allocated. */
    else if (l2_entry & QCOW2_L2_ENTRY_TYPE_MASK)
      e.type = 0;
    /* From here on we know this is a standard cluster because we
     * handled compressed clusters above and we don't support extended
     * clusters.
     */
    else if ((l2_entry & QCOW2_L2_ENTRY_RESERVED_MASK) != 0) {
      nbdkit_error ("invalid L2 table entry: "
                    "reserved bits are not zero (0x%" PRIx64 ")",
                    l2_entry);
      *err = ERANGE;
      return -1;
    }
    else {
      file_offset = l2_entry & QCOW2_L2_ENTRY_OFFSET_MASK;

      /* Does the cluster read as all zeroes?  Note we can check
       * file_offset == 0 here because we don't support external files.
       */
      if ((l2_entry & 1) != 0 || file_offset == 0)
        e.type = NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO;
      else
        /* Regular allocated non-compressed cluster. */
        e.type = 0;
    }

    if (nbdkit_add_extent (extents, e.offset, e.length, e.type) == -1) {
      *err = errno;
      return -1;
    }

    /* If the caller only wanted the first extent, and we've managed
     * to add at least one extent to the list, then we can drop out
     * now.  (Note calling nbdkit_add_extent above does not mean the
     * extent got added since it might be before the first offset.)
     */
    if (req_one && nbdkit_extents_count (extents) > 0)
      break;

    offset += cluster_size;
    count -= cluster_size;
  }

  return 0;
}

static struct nbdkit_filter filter = {
  .name              = "qcow2dec",
  .longname          = "nbdkit qcow2dec filter",
  .unload            = qcow2dec_unload,
  .dump_plugin       = qcow2dec_dump_plugin,
  .can_write         = qcow2dec_can_write,
  .can_cache         = qcow2dec_can_cache,
  .can_multi_conn    = qcow2dec_can_multi_conn,
  .can_extents       = qcow2dec_can_extents,
  .prepare           = qcow2dec_prepare,
  .get_size          = qcow2dec_get_size,
  .pread             = qcow2dec_pread,
  .extents           = qcow2dec_extents,
};

NBDKIT_REGISTER_FILTER (filter)
