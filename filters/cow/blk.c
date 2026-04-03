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

/* Notes on the design and implementation of this filter:
 *
 * The filter works by creating a large, sparse temporary file, the
 * same size as the underlying device.  Being sparse, initially this
 * takes up no space.
 *
 * We confine all pread/pwrite operations to the filesystem block
 * size.  The blk_* functions below only work on whole filesystem
 * block boundaries.  A smaller-than-block-size pwrite will turn into
 * a read-modify-write of a whole block.  We also assume that the
 * plugin returns the same immutable data for each pread call we make,
 * and optimize on this basis.
 *
 * A 2-bit per block bitmap is maintained in memory recording if each
 * block in the temporary file is:
 *
 *   00 = not allocated in the overlay (read through to the plugin)
 *   01 = allocated in the overlay
 *   10 = <unused>
 *   11 = trimmed in the overlay
 *
 * When reading a block we first check the bitmap to see if that file
 * block is allocated, trimmed or not.  If allocated, we return it
 * from the temporary file.  Trimmed returns zeroes.  If not allocated
 * we issue a pread to the underlying plugin.
 *
 * When writing a block we unconditionally write the data to the
 * temporary file, setting the bit in the bitmap.  (Writing zeroes is
 * handled the same way.)
 *
 * When trimming we set the trimmed flag in the bitmap for whole
 * blocks, and handle the unaligned portions like writing zeroes
 * above.  We could punch holes in the overlay as an optimization, but
 * for simplicity we do not do that yet.
 *
 * Since the overlay is a deleted temporary file, we can ignore FUA
 * and flush commands.
 *
 * For disk sizes larger than 8T, the temporary file is split into
 * multiple files, at most 8T in size.  This ensures we can work on
 * filesystems with file size limits like ext4 (max file size < 16T).
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <pthread.h>

#include <nbdkit-filter.h>

#include "bitmap.h"
#include "cleanup.h"
#include "fdatasync.h"
#include "rounding.h"
#include "pread.h"
#include "pwrite.h"
#include "utils.h"
#include "vector.h"

#include "cow.h"
#include "blk.h"

enum bm_entry {
  BLOCK_NOT_ALLOCATED = 0,
  BLOCK_ALLOCATED = 1,
  BLOCK_TRIMMED = 3,
};

/* Maximum overlay file size, 8T.  This must be a power of 2. */
#define MAX_FILE_SIZE (UINT64_C (8) * 1024 * 1024 * 1024 * 1024)

static const char *
state_to_string (enum bm_entry state)
{
  switch (state) {
  case BLOCK_NOT_ALLOCATED: return "not allocated";
  case BLOCK_ALLOCATED: return "allocated";
  case BLOCK_TRIMMED: return "trimmed";
  default: abort ();
  }
}

/* Extra debugging (-D cow.verbose=1). */
NBDKIT_DLL_PUBLIC int cow_debug_verbose = 0;

static char *template;

void
blk_load (void)
{
  const char *tmpdir = getenv ("TMPDIR");
  if (!tmpdir)
    tmpdir = LARGE_TMPDIR;

  nbdkit_debug ("cow: temporary directory for overlay: %s", tmpdir);

  if (asprintf (&template, "%s/XXXXXX", tmpdir) == -1) {
    nbdkit_error ("asprintf: %m");
    exit (EXIT_FAILURE);
  }
}

void
blk_unload (void)
{
  free (template);
}

DEFINE_VECTOR_TYPE (fd_vector, int);

struct blk_overlay {
  /* The temporary overlays.  Each covers 8T virtual size. */
  fd_vector fds;

  /* This lock protects the bitmap from parallel access. */
  pthread_mutex_t lock;

  /* Bitmap. */
  struct bitmap bm;

  /* Because blk_set_size is called before the other blk_* functions
   * this should be set to the true size before we need it.
   */
  uint64_t size;
};

static int
create_temporary_file (void)
{
  int fd;
  char *filename;

  filename = strdup (template);
  if (filename == NULL) {
    nbdkit_error ("strdup: %m");
    return -1;
  }

#ifdef HAVE_MKOSTEMP
  fd = mkostemp (filename, O_CLOEXEC);
#else
  /* Not atomic, but this is only invoked during .load, so the race
   * won't affect any plugin actions trying to fork
   */
  fd = mkstemp (filename);
  if (fd >= 0) {
    fd = set_cloexec (fd);
    if (fd < 0) {
      int e = errno;
      unlink (filename);
      errno = e;
    }
  }
#endif
  if (fd == -1) {
    nbdkit_error ("mkostemp: %s: %m", template);
    free (filename);
    return -1;
  }

  if (cow_debug_verbose)
    nbdkit_debug ("cow: created temporary file for overlay: %s", filename);

  unlink (filename);
  free (filename);

  return fd;
}

struct blk_overlay *
blk_create (void)
{
  struct blk_overlay *blk;
  int fd;

  blk = calloc (1, sizeof *blk);
  if (blk == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  blk->fds = (fd_vector)empty_vector;

  /* We always create the first file, even if the disk size is 0. */
  fd = create_temporary_file ();
  if (fd == -1) {
    free (blk);
    return NULL;
  }
  if (fd_vector_append (&blk->fds, fd) == -1) {
    nbdkit_error ("realloc: %m");
    close (fd);
    free (blk);
    return NULL;
  }

  pthread_mutex_init (&blk->lock, NULL);
  bitmap_init (&blk->bm, blksize, 2 /* bits per block */);

  return blk;
}

void
blk_free (struct blk_overlay *blk)
{
  if (blk) {
    fd_vector_iter (&blk->fds, (void*)close);
    bitmap_free (&blk->bm);
    pthread_mutex_destroy (&blk->lock);
    free (blk);
  }
}

/* Allocate or resize the overlay file and bitmap. */
int
blk_set_size (struct blk_overlay *blk, uint64_t new_size)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
  const uint64_t new_size_rounded_up = ROUND_UP (new_size, blksize);
  uint64_t remainder;
  size_t i, n;
  int fd;

  blk->size = new_size;

  if (bitmap_resize (&blk->bm, blk->size) == -1)
    return -1;

  /* How many temporary files do we need?  Each temp file is limited to 8T. */
  n = DIV_ROUND_UP (new_size_rounded_up, MAX_FILE_SIZE);
  if (n == 0)
    n = 1;
  nbdkit_debug ("cow: using %zu temporary file(s)", n);

  /* Create new temporary files in case it has expanded. */
  while (blk->fds.len < n) {
    fd = create_temporary_file ();
    if (fd == -1)
      return -1;
    if (fd_vector_append (&blk->fds, fd) == -1) {
      nbdkit_error ("realloc: %m");
      /* The other fds are stored in the handle and will be closed by
       * blk_free, so we only need to close this one.
       */
      close (fd);
      return -1;
    }
  }

  /* Remove any extra temporary files in case it has shrunk. */
  while (blk->fds.len > n) {
    close (blk->fds.ptr[n]);
    fd_vector_remove (&blk->fds, n);
  }

  /* The first 0..n-1 temp files have to be truncated to the full
   * size, with the last temp file being truncated to the remainder.
   */
  remainder = new_size_rounded_up;
  for (i = 0; i < n-1; ++i) {
    if (ftruncate (blk->fds.ptr[i], MAX_FILE_SIZE) == -1) {
      nbdkit_error ("cow: ftruncate: %m");
      return -1;
    }
    assert (remainder >= MAX_FILE_SIZE);
    remainder -= MAX_FILE_SIZE;
  }
  if (ftruncate (blk->fds.ptr[n-1], remainder) == -1) {
    nbdkit_error ("cow: ftruncate: %m");
    return -1;
  }

  return 0;
}

/* Get the temporary file corresponding to a block. */
static int
get_fd_for_blknum (struct blk_overlay *blk, uint64_t blknum)
{
  size_t i = blknum * blksize / MAX_FILE_SIZE;
  assert (i < blk->fds.len);
  return blk->fds.ptr[i];
}

/* This is a bit of a hack since usually this information is hidden in
 * the blk module.  However it is needed when calculating extents.
 */
void
blk_status (struct blk_overlay *blk,
            uint64_t blknum, bool *present, bool *trimmed)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
  enum bm_entry state = bitmap_get_blk (&blk->bm, blknum, BLOCK_NOT_ALLOCATED);

  *present = state != BLOCK_NOT_ALLOCATED;
  *trimmed = state == BLOCK_TRIMMED;
}

/* These are the block operations.  They always read or write whole
 * blocks of size ‘blksize’.
 */
int
blk_read_multiple (struct blk_overlay *blk,
                   nbdkit_next *next,
                   uint64_t blknum, uint64_t nrblocks,
                   uint8_t *block, bool cow_on_read, int *err)
{
  const uint64_t offset = blknum * blksize;
  enum bm_entry state;
  uint64_t b, runblocks;
  const int fd = get_fd_for_blknum (blk, blknum);

  /* Find out how many of the following blocks form a "run" with the
   * same state.  We can process that many blocks in one go.
   *
   * About the locking: The state might be modified from another
   * thread - for example another thread might write
   * (BLOCK_NOT_ALLOCATED -> BLOCK_ALLOCATED) while we are reading
   * from the plugin, returning the old data.  However a read issued
   * after the write returns should always return the correct data.
   */
  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
    state = bitmap_get_blk (&blk->bm, blknum, BLOCK_NOT_ALLOCATED);

    for (b = 1, runblocks = 1; b < nrblocks; ++b, ++runblocks) {
      enum bm_entry s =
        bitmap_get_blk (&blk->bm, blknum + b, BLOCK_NOT_ALLOCATED);
      if (state != s)
        break;
      if (get_fd_for_blknum (blk, blknum + b) != fd)
        break;
    }
  }

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_read_multiple block %" PRIu64
                  " (offset %" PRIu64 ") run of length %" PRIu64
                  " is %s",
                  blknum, offset, runblocks,
                  state_to_string (state));

  if (state == BLOCK_NOT_ALLOCATED) { /* Read underlying plugin. */
    unsigned n, tail = 0;

    assert (blksize * runblocks <= UINT_MAX);
    n = blksize * runblocks;

    if (offset + n > blk->size) {
      tail = offset + n - blk->size;
      n -= tail;
    }

    if (next->pread (next, block, n, offset, 0, err) == -1)
      return -1;

    /* Normally we're reading whole blocks, but at the very end of the
     * file we might read a partial block.  Deal with that case by
     * zeroing the tail.
     */
    memset (block + n, 0, tail);

    /* If cow-on-read is true then copy the blocks to the cache and
     * set them as allocated.
     */
    if (cow_on_read) {
      if (cow_debug_verbose)
        nbdkit_debug ("cow: cow-on-read saving %" PRIu64 " blocks "
                      "at offset %" PRIu64 " into the cache "
                      "(fd %d file offset %" PRIu64 ")",
                      runblocks, offset,
                      fd, offset & (MAX_FILE_SIZE - 1));

      if (full_pwrite (fd, block, blksize * runblocks,
                       offset & (MAX_FILE_SIZE - 1)) == -1) {
        *err = errno;
        nbdkit_error ("cow: pwrite: %m");
        return -1;
      }
      for (b = 0; b < runblocks; ++b)
        bitmap_set_blk (&blk->bm, blknum+b, BLOCK_ALLOCATED);
    }
  }
  else if (state == BLOCK_ALLOCATED) { /* Read overlay. */
      if (cow_debug_verbose)
        nbdkit_debug ("cow: reading overlay %" PRIu64 " blocks "
                      "at offset %" PRIu64 " "
                      "(fd %d file offset %" PRIu64 ")",
                      runblocks, offset,
                      fd, offset & (MAX_FILE_SIZE - 1));

    if (full_pread (fd, block, blksize * runblocks,
                    offset & (MAX_FILE_SIZE - 1)) == -1) {
      *err = errno;
      nbdkit_error ("cow: pread: %m");
      return -1;
    }
  }
  else /* state == BLOCK_TRIMMED */ {
    memset (block, 0, blksize * runblocks);
  }

  /* If all done, return. */
  if (runblocks == nrblocks)
    return 0;

  /* Recurse to read remaining blocks. */
  return blk_read_multiple (blk, next,
                            blknum + runblocks,
                            nrblocks - runblocks,
                            block + blksize * runblocks,
                            cow_on_read, err);
}

int
blk_read (struct blk_overlay *blk,
          nbdkit_next *next,
          uint64_t blknum, uint8_t *block, bool cow_on_read, int *err)
{
  return blk_read_multiple (blk, next, blknum, 1, block, cow_on_read, err);
}

int
blk_cache (struct blk_overlay *blk,
           nbdkit_next *next,
           uint64_t blknum, uint8_t *block, enum cache_mode mode, int *err)
{
  /* XXX Could make this lock more fine-grained with some thought. */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
  const uint64_t offset = blknum * blksize;
  enum bm_entry state = bitmap_get_blk (&blk->bm, blknum, BLOCK_NOT_ALLOCATED);
  unsigned n = blksize, tail = 0;
  const int fd = get_fd_for_blknum (blk, blknum);

  if (offset + n > blk->size) {
    tail = offset + n - blk->size;
    n -= tail;
  }

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_cache block %" PRIu64 " (offset %" PRIu64 ") is %s "
                  "(fd %d file offset %" PRIu64 ")",
                  blknum, offset, state_to_string (state),
                  fd, offset & (MAX_FILE_SIZE - 1));

  if (state == BLOCK_ALLOCATED) {
#if HAVE_POSIX_FADVISE
    int r = posix_fadvise (fd, offset & (MAX_FILE_SIZE - 1),
                           blksize, POSIX_FADV_WILLNEED);
    if (r) {
      errno = r;
      nbdkit_error ("cow: posix_fadvise: %m");
      return -1;
    }
#endif
    return 0;
  }
  if (state == BLOCK_TRIMMED)
    return 0;
  if (mode == BLK_CACHE_IGNORE)
    return 0;
  if (mode == BLK_CACHE_PASSTHROUGH)
    return next->cache (next, n, offset, 0, err);

  if (next->pread (next, block, n, offset, 0, err) == -1)
    return -1;
  /* Normally we're reading whole blocks, but at the very end of the
   * file we might read a partial block.  Deal with that case by
   * zeroing the tail.
   */
  memset (block + n, 0, tail);

  if (mode == BLK_CACHE_COW) {
    if (full_pwrite (fd, block, blksize,
                     offset & (MAX_FILE_SIZE - 1)) == -1) {
      *err = errno;
      nbdkit_error ("cow: pwrite: %m");
      return -1;
    }
    bitmap_set_blk (&blk->bm, blknum, BLOCK_ALLOCATED);
  }
  return 0;
}

int
blk_write (struct blk_overlay *blk,
           uint64_t blknum, const uint8_t *block, int *err)
{
  const uint64_t offset = blknum * blksize;
  const int fd = get_fd_for_blknum (blk, blknum);

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_write block %" PRIu64 " at offset %" PRIu64 " "
                  "(fd %d file offset %" PRIu64 ")",
                  blknum, offset,
                  fd, offset & (MAX_FILE_SIZE - 1));

  if (full_pwrite (fd, block, blksize,
                   offset & (MAX_FILE_SIZE - 1)) == -1) {
    *err = errno;
    nbdkit_error ("cow: pwrite: %m");
    return -1;
  }

  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
  bitmap_set_blk (&blk->bm, blknum, BLOCK_ALLOCATED);

  return 0;
}

int
blk_trim (struct blk_overlay *blk,
          uint64_t blknum, int *err)
{
  const uint64_t offset = blknum * blksize;

  if (cow_debug_verbose)
    nbdkit_debug ("cow: blk_trim block %" PRIu64 " at offset %" PRIu64,
                  blknum, offset);

  /* XXX As an optimization we could punch a whole in the overlay
   * here.  However it's not trivial since blksize is unrelated to the
   * overlay filesystem block size.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&blk->lock);
  bitmap_set_blk (&blk->bm, blknum, BLOCK_TRIMMED);
  return 0;
}
