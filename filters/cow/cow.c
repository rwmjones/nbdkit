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
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>

#include <nbdkit-filter.h>

#include "blk.h"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* In order to handle parallel requests safely, this lock must be held
 * when calling any blk_* functions.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void
cow_load (void)
{
  if (blk_init () == -1)
    exit (EXIT_FAILURE);
}

static void
cow_unload (void)
{
  blk_free ();
}

static void *
cow_open (nbdkit_next_open *next, void *nxdata, int readonly)
{
  /* Always pass readonly=1 to the underlying plugin. */
  if (next (nxdata, 1) == -1)
    return NULL;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the file size and ensure the overlay is the correct size. */
static int64_t
cow_get_size (struct nbdkit_next_ops *next_ops, void *nxdata,
              void *handle)
{
  int64_t size;
  int r;

  size = next_ops->get_size (nxdata);
  if (size == -1)
    return -1;

  nbdkit_debug ("cow: underlying file size: %" PRIi64, size);

  pthread_mutex_lock (&lock);
  r = blk_set_size (size);
  pthread_mutex_unlock (&lock);
  if (r == -1)
    return -1;

  return size;
}

/* Force an early call to cow_get_size, consequently truncating the
 * overlay to the correct size.
 */
static int
cow_prepare (struct nbdkit_next_ops *next_ops, void *nxdata,
             void *handle)
{
  int64_t r;

  r = cow_get_size (next_ops, nxdata, handle);
  return r >= 0 ? 0 : -1;
}

/* Whatever the underlying plugin can or can't do, we can write, we
 * cannot trim, and we can flush.
 */
static int
cow_can_write (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 1;
}

static int
cow_can_trim (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 0;
}

static int
cow_can_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return 1;
}

static int
cow_can_fua (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle)
{
  return NBDKIT_FUA_EMULATE;
}

static int cow_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle, uint32_t flags, int *err);

/* Read data. */
static int
cow_pread (struct nbdkit_next_ops *next_ops, void *nxdata,
           void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    memcpy (buf, &block[blkoffs], n);

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  return 0;
}

/* Write data. */
static int
cow_pwrite (struct nbdkit_next_ops *next_ops, void *nxdata,
            void *handle, const void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     */
    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memcpy (&block[blkoffs], buf, n);
      r = blk_write (blknum, block, err);
    }
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    buf += n;
    count -= n;
    offset += n;
  }

  free (block);
  if (flags & NBDKIT_FLAG_FUA)
    return cow_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

/* Zero data. */
static int
cow_zero (struct nbdkit_next_ops *next_ops, void *nxdata,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  uint8_t *block;

  block = malloc (BLKSIZE);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    uint64_t blknum, blkoffs, n;
    int r;

    blknum = offset / BLKSIZE;  /* block number */
    blkoffs = offset % BLKSIZE; /* offset within the block */
    n = BLKSIZE - blkoffs;      /* max bytes we can read from this block */
    if (n > count)
      n = count;

    /* Do a read-modify-write operation on the current block.
     * Hold the lock over the whole operation.
     *
     * XXX There is the possibility of optimizing this: ONLY if we are
     * writing a whole, aligned block, then use FALLOC_FL_ZERO_RANGE.
     */
    pthread_mutex_lock (&lock);
    r = blk_read (next_ops, nxdata, blknum, block, err);
    if (r != -1) {
      memset (&block[blkoffs], 0, n);
      r = blk_write (blknum, block, err);
    }
    pthread_mutex_unlock (&lock);
    if (r == -1) {
      free (block);
      return -1;
    }

    count -= n;
    offset += n;
  }

  free (block);
  if (flags & NBDKIT_FLAG_FUA)
    return cow_flush (next_ops, nxdata, handle, 0, err);
  return 0;
}

static int
cow_flush (struct nbdkit_next_ops *next_ops, void *nxdata, void *handle,
           uint32_t flags, int *err)
{
  int r;

  pthread_mutex_lock (&lock);
  r = blk_flush ();
  if (r == -1)
    *err = errno;
  pthread_mutex_unlock (&lock);
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "cow",
  .longname          = "nbdkit copy-on-write (COW) filter",
  .version           = PACKAGE_VERSION,
  .load              = cow_load,
  .unload            = cow_unload,
  .open              = cow_open,
  .prepare           = cow_prepare,
  .get_size          = cow_get_size,
  .can_write         = cow_can_write,
  .can_flush         = cow_can_flush,
  .can_trim          = cow_can_trim,
  .can_fua           = cow_can_fua,
  .pread             = cow_pread,
  .pwrite            = cow_pwrite,
  .zero              = cow_zero,
  .flush             = cow_flush,
};

NBDKIT_REGISTER_FILTER(filter)
