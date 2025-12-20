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

#ifndef NBDKIT_BLK_H
#define NBDKIT_BLK_H

struct blk_overlay;

extern void blk_load (void);
extern void blk_unload (void);

/* Initialize a new overlay and bitmap. */
extern struct blk_overlay *blk_create (void);

/* Close the overlay, free the bitmap. */
extern void blk_free (struct blk_overlay *blk);

/* Allocate or resize the overlay and bitmap. */
extern int blk_set_size (struct blk_overlay *blk, uint64_t new_size)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

/* Returns the status of the block in the overlay. */
extern void blk_status (struct blk_overlay *blk,
                        uint64_t blknum, bool *present, bool *trimmed)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

/* Read a single block from the overlay or plugin. */
extern int blk_read (struct blk_overlay *blk,
                     nbdkit_next *next,
                     uint64_t blknum, uint8_t *block,
                     bool cow_on_read, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 4, 6));

/* Read multiple blocks from the overlay or plugin. */
extern int blk_read_multiple (struct blk_overlay *blk,
                              nbdkit_next *next,
                              uint64_t blknum, uint64_t nrblocks,
                              uint8_t *block,
                              bool cow_on_read, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 5, 7));

/* Cache mode for blocks not already in overlay */
enum cache_mode {
  BLK_CACHE_IGNORE,      /* Do nothing */
  BLK_CACHE_PASSTHROUGH, /* Make cache request to plugin */
  BLK_CACHE_READ,        /* Make ignored read request to plugin */
  BLK_CACHE_COW,         /* Make read request to plugin, and write to overlay */
};

/* Cache a single block from the plugin. */
extern int blk_cache (struct blk_overlay *blk,
                      nbdkit_next *next,
                      uint64_t blknum, uint8_t *block, enum cache_mode,
                      int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 4, 6));

/* Write a single block. */
extern int blk_write (struct blk_overlay *blk,
                      uint64_t blknum, const uint8_t *block, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3, 4));

/* Trim a single block. */
extern int blk_trim (struct blk_overlay *blk,
                     uint64_t blknum, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3));

#endif /* NBDKIT_BLK_H */
