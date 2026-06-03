/* nbdkit
 * Copyright (C) 2026 Richard W.M. Jones
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
#include <assert.h>

#include <nbdkit-filter.h>

#include "isaligned.h"
#include "minmax.h"
#include "random.h"
#include "rounding.h"

enum mode {
  mode_not_set = 0,
  mode_value,                   /* xor=VALUE */
  mode_rand,                    /* xor=rand[:SEED] */
};
static enum mode mode = mode_not_set;

/* In value mode, the 'xor=VALUE' parameter from the command line. */
static uint64_t valueparam;

/* Value as bytes, big endian, rotated so that xor[0..xorlen-1] are
 * what we will XOR with the data.
 */
static uint8_t xor[8];

/* Length of value.  Valid lengths are [1..8], or 0 if not yet set. */
static size_t xorlen = 0;

/* In rand mode, the seed. */
static uint64_t seed;

static int
xor_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
            const char *key, const char *value)
{
  if (strcmp (key, "xor") == 0) {
    if (mode != mode_not_set) {
      nbdkit_error ("'xor' parameter must be set only once");
      return -1;
    }
    if (strcmp (value, "rand") == 0) {
      if (xorlen) {
        nbdkit_error ("'xorlen' must not be used with 'xor=rand'");
        return -1;
      }
      mode = mode_rand;
      xorlen = 8;
      seed = 0;
    }
    else if (strncmp (value, "rand:", 5) == 0) {
      if (xorlen) {
        nbdkit_error ("'xorlen' must not be used with 'xor=rand'");
        return -1;
      }
      mode = mode_rand;
      xorlen = 8;
      if (nbdkit_parse_uint64_t (key, value + 5, &seed) == -1)
        return -1;
    }
    else {
      mode = mode_value;
      if (nbdkit_parse_uint64_t (key, value, &valueparam) == -1)
        return -1;
    }
    return 0;
  }
  else if (strcmp (key, "xorlen") == 0) {
    int n;

    if (xorlen) {
      nbdkit_error ("'xorlen' parameter must be set only once");
      return -1;
    }
    if (nbdkit_parse_int (key, value, &n) == -1)
      return -1;
    if (n <= 0 || n > 8) {
      nbdkit_error ("'xorlen' parameter must be [1..8] (in bytes)");
      return -1;
    }
    xorlen = n;
    return 0;
  }
  else
    return next (nxdata, key, value);
}

static int
xor_config_complete (nbdkit_next_config_complete *next,
                     nbdkit_backend *nxdata)
{
  if (mode == mode_not_set) {
    nbdkit_error ("'xor' parameter is required");
    return -1;
  }
  if (xorlen == 0) {
    nbdkit_error ("'xorlen' parameter is required");
    return -1;
  }

  return next (nxdata);
}

#define xor_config_help \
  "xor=VALUE                  Value to be XORed with data, eg. 0x1234.\n" \
  "xor=rand[:SEED]            XOR with pseudo-random numbers with optional seed.\n" \
  "xorlen=LENBYTES            Length of value in bytes (range [1..8])."

static int
xor_get_ready (int final_thread_mode)
{
  switch (mode) {
  case mode_not_set:
    abort ();
  case mode_value: {
    uint64_t value = valueparam;
    ssize_t i;

    /* Get the actual bytes we will XOR with the data. */
    for (i = xorlen-1; i >= 0; --i) {
      xor[i] = value & 0xff;
      value >>= 8;
    }
    nbdkit_debug ("xor[] = { "
                  "0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", "
                  "0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8 ", 0x%" PRIx8
                  " }",
                  xor[0], xor[1], xor[2], xor[3],
                  xor[4], xor[5], xor[6], xor[7]);
    break;
  }
  case mode_rand:
    break;
  }
  return 0;
}

/*-- For xor=VALUE this is where we do in-place XOR. --*/

/* Break out the simple, aligned cases first to give GCC a good chance
 * of vectorizing.
 */

/* xorlen == 1 */
static void
do_xor1 (uint8_t *buf, uint32_t count, uint8_t c)
{
  size_t i;

  for (i = 0; i < count; ++i)
    buf[i] ^= c;
}

/* xorlen == 2 and aligned to 2 bytes */
static void
do_aligned_xor2 (uint16_t *buf, uint32_t count, uint16_t c)
{
  size_t i;

  for (i = 0; i < count/2 /* because bytes */; ++i)
    buf[i] ^= c;
}

/* xorlen == 4 and aligned to 4 bytes */
static void
do_aligned_xor4 (uint32_t *buf, uint32_t count, uint32_t c)
{
  size_t i;

  for (i = 0; i < count/4 /* because bytes */; ++i)
    buf[i] ^= c;
}

/* xorlen == 8 and aligned to 8 bytes */
static void
do_aligned_xor8 (uint64_t *buf, uint32_t count, uint64_t c)
{
  size_t i;

  for (i = 0; i < count/8 /* because bytes */; ++i)
    buf[i] ^= c;
}

static void
do_xor_value (void *bufv, uint32_t count, uint64_t offset)
{
  uint8_t *buf = bufv;
  size_t i;

  /* This check makes the recursion easier. */
  if (count == 0) return;

  if (xorlen == 1) {
    do_xor1 (buf, count, xor[0]);
    return;
  }
  if (xorlen == 2 && IS_ALIGNED (offset, 2) && IS_ALIGNED (count, 2)) {
    uint16_t c = * (uint16_t *) xor;
    do_aligned_xor2 (bufv, count, c);
    return;
  }
  if (xorlen == 4 && IS_ALIGNED (offset, 4) && IS_ALIGNED (count, 4)) {
    uint32_t c = * (uint32_t *) xor;
    do_aligned_xor4 (bufv, count, c);
    return;
  }
  if (xorlen == 8 && IS_ALIGNED (offset, 8) && IS_ALIGNED (count, 8)) {
    uint64_t c = * (uint64_t *) xor;
    do_aligned_xor8 (bufv, count, c);
    return;
  }

  /* Align to the next boundary. */
  while (count > 0 && (i = (offset % xorlen)) != 0) {
    *buf ^= xor[i];
    offset++;
    count--;
    buf++;
  }
  if (count == 0) return;

  /* Do the aligned part quickly if we can. */
  if (xorlen == 2 || xorlen == 4 || xorlen == 8) {
    size_t n = ROUND_DOWN (count, xorlen);
    do_xor_value (buf, n, offset);
    offset += n;
    count -= n;
    buf += n;
    if (count == 0) return;
  }

  /* Do at least one byte so we make progress.
   * We know we are aligned here.
   */
  *buf ^= xor[0];
  offset++;
  count--;
  buf++;

  /* Recurse for the rest. */
  do_xor_value (buf, count, offset);
}

/*-- Do xor=rand.  Note that xorlen is always 8. --*/

/* See also random.c:generate() */
static uint64_t
get_rand_at_offset (uint64_t offset)
{
  struct random_state state;
  uint64_t r;

  assert ((offset & 7) == 0);

  xsrandom (seed + offset, &state);
  r = xrandom (&state);
  return r;
}

static void
do_aligned_rand8 (uint64_t *buf, uint32_t count, uint64_t offset)
{
  size_t i;

  for (i = 0; i < count; i += 8) {
    uint64_t r = get_rand_at_offset (offset + i);
    buf[i/8] ^= r;
  }
}

static void
do_xor_rand (void *bufv, uint32_t count, uint64_t offset)
{
  uint8_t *buf = bufv;
  size_t i, n;
  union {
    uint64_t r;
    uint8_t rand[8];
  } u;

  /* This check makes the recursion easier. */
  if (count == 0) return;

  if (IS_ALIGNED (offset, 8) && IS_ALIGNED (count, 8)) {
    do_aligned_rand8 (bufv, count, offset);
    return;
  }

  /* Align to the next boundary. */
  while (count > 0 && (i = (offset % 8)) != 0) {
    u.r = get_rand_at_offset (ROUND_DOWN (offset, 8));
    *buf ^= u.rand[i];
    offset++;
    count--;
    buf++;
  }
  if (count == 0) return;

  /* Do the aligned part quickly if we can. */
  n = ROUND_DOWN (count, 8);
  do_xor_rand (buf, n, offset);
  offset += n;
  count -= n;
  buf += n;
  if (count == 0) return;

  /* Do at least one byte so we make progress.
   * We know we are aligned here.
   */
  u.r = get_rand_at_offset (offset);
  *buf ^= u.rand[0];
  offset++;
  count--;
  buf++;

  /* Recurse for the rest. */
  do_xor_rand (buf, count, offset);
}

static void
do_xor (void *bufv, uint32_t count, uint64_t offset)
{
  switch (mode) {
  case mode_not_set:
    abort ();
  case mode_value:
    do_xor_value (bufv, count, offset);
    return;
  case mode_rand:
    assert (xorlen == 8);
    do_xor_rand (bufv, count, offset);
    return;
  }
}

/* Read data. */
static int
xor_pread (nbdkit_next *next,
           void *handle, void *buf, uint32_t count, uint64_t offset,
           uint32_t flags, int *err)
{
  int r;

  r = next->pread (next, buf, count, offset, flags, err);
  if (r == -1)
    return -1;

  do_xor (buf, count, offset);
  return 0;
}

/* Write data. */
static int
xor_pwrite (nbdkit_next *next,
            void *handle, const void *buf, uint32_t count, uint64_t offset,
            uint32_t flags, int *err)
{
  void *block;
  int r;

  block = malloc (count);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  memcpy (block, buf, count);
  do_xor (block, count, offset);

  r = next->pwrite (next, block, count, offset, flags, err);
  free (block);
  return r;
}

/* Trim or zero data. */
static int
xor_trim (nbdkit_next *next,
          void *handle, uint32_t count, uint64_t offset, uint32_t flags,
          int *err)
{
  const bool fua = flags & NBDKIT_FLAG_FUA;
  uint32_t pwrite_flags = 0;
  void *block;
  const size_t zero_max = 65536;
  size_t len;
  int r = 0;

  if (fua && next->can_fua (next) == NBDKIT_FUA_NATIVE)
    pwrite_flags |= NBDKIT_FLAG_FUA;

  block = malloc (zero_max);
  if (block == NULL) {
    *err = errno;
    nbdkit_error ("malloc: %m");
    return -1;
  }

  while (count > 0) {
    len = MIN (count, zero_max);
    memset (block, 0, len);
    do_xor (block, len, offset);
    r = next->pwrite (next, block, len, offset, pwrite_flags, err);
    if (r == -1) goto out;
    count -= len;
    offset += len;
  }

 out:
  free (block);
  return r;
}

static struct nbdkit_filter filter = {
  .name              = "xor",
  .longname          = "nbdkit XOR filter",
  .config            = xor_config,
  .config_complete   = xor_config_complete,
  .config_help       = xor_config_help,
  .get_ready         = xor_get_ready,
  .pread             = xor_pread,
  .pwrite            = xor_pwrite,
  .trim              = xor_trim,
  .zero              = xor_trim,
};

NBDKIT_REGISTER_FILTER (filter)
