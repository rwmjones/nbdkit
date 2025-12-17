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
#include <assert.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "byte-swapping.h"
#include "cleanup.h"
#include "isaligned.h"
#include "ispowerof2.h"
#include "minmax.h"
#include "random.h"
#include "rounding.h"

/* The size of disk in bytes (initialized by size=<SIZE> parameter). */
static uint64_t size = 0;
static uint64_t stride = 8;
static uint64_t upper = 0;      /* already shifted up by 48 bits */

static struct random_state random_state;

static void
pattern_load (void)
{
  xsrandom (time (NULL), &random_state);
}

static int
pattern_config (const char *key, const char *value)
{
  int64_t r;

  if (strcmp (key, "size") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    size = r;
  }
  else if (strcmp (key, "stride") == 0) {
    r = nbdkit_parse_size (value);
    if (r == -1)
      return -1;
    if (r < 8 || !is_power_of_2 (r)) {
      nbdkit_error ("stride must be >= 8 and a power of two");
      return -1;
    }
    stride = r;
  }
  else if (strcmp (key, "upper") == 0) {
    if (strcmp (value, "random") == 0) {
      do {
        upper = xrandom (&random_state) & 0xffff;
      } while (upper == 0);
    }
    else {
      uint16_t u16;

      if (nbdkit_parse_uint16_t ("upper", value, &u16) == -1)
        return -1;
      upper = u16;
    }
    upper <<= 48;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define pattern_config_help \
  "size=<SIZE>  (required) Size of the backing disk\n" \
  "stride=<BLOCK_SIZE>     Block size (stride) of pattern\n" \
  "upper=BITS|random       Set upper 16 bits"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Create the per-connection handle. */
static void *
pattern_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* Get the disk size. */
static int64_t
pattern_get_size (void *handle)
{
  return size;
}

/* Serves the same data over multiple connections. */
static int
pattern_can_multi_conn (void *handle)
{
  return 1;
}

/* Cache. */
static int
pattern_can_cache (void *handle)
{
  /* Everything is already in memory, returning this without
   * implementing .cache lets nbdkit do the correct no-op.
   */
  return NBDKIT_CACHE_NATIVE;
}

/* Read data. */
static int
pattern_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  uint8_t *b = buf;
  uint64_t d, rem, o, n;

  /* If stride > 8 then there are gaps between the offsets.  It's
   * easier and faster to zero out the whole buffer first in this
   * unusual case.
   */
  if (stride > 8)
    memset (buf, 0, count);

  while (count > 0) {
    /* If the request is aligned to the stride, we can go ahead and
     * write into the buffer directly.
     */
    if (IS_ALIGNED (offset, stride) && IS_ALIGNED (count, stride)) {
      while (count > 0) {
        assert (count >= stride); /* must be true because of alignment */
        d = htobe64 (upper ^ offset);
        memcpy (b, (uint8_t *) &d, 8);
        count -= stride;
        offset += stride;
        b += stride;
      }
    }
    /* The slow path case. */
    else {
      /* rem = bytes remaining until next stride boundary
       * example: stride is 32
       * |oooooooo------------------------|oooooooo----...
       *     ^ offset                     ^ next stride boundary
       *     rem = 32-3
       */
      rem = ROUND_UP (offset, stride) - offset;
      if (rem == 0) rem = stride;
      if (rem > stride-8) {     /* writing the offset */
        d = htobe64 (upper ^ ROUND_DOWN (offset, stride));
        o = offset & 7;
        n = MIN (count, 8-o);
        memcpy (b, (char *) &d + o, n);
      }
      else {                    /* after the offset, skip zeroes */
        n = MIN (count, rem);
      }
      b += n;
      offset += n;
      count -= n;
      /* We're (probably) on a stride boundary now which may mean we're
       * on the fast path.  If not, we'll end up back here on the slow
       * path.
       */
    }
  }

  return 0;
}

/* Write data.
 *
 * This verifies that the data matches what is read.  This is
 * implemented by calling pattern_pread above internally and comparing
 * the two buffers.
 */
static int
pattern_pwrite (void *handle, const void *buf,
                uint32_t count, uint64_t offset,
                uint32_t flags)
{
  CLEANUP_FREE char *expected = malloc (count);
  if (expected == NULL) {
    nbdkit_error ("malloc: %m");
    return -1;
  }

  if (pattern_pread (handle, expected, count, offset, flags) == -1)
    return -1;

  if (memcmp (buf, expected, count) != 0) {
    errno = EIO;
    nbdkit_error ("data written does not match expected");
    return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "pattern",
  .version           = PACKAGE_VERSION,
  .load              = pattern_load,
  .config            = pattern_config,
  .config_help       = pattern_config_help,
  .magic_config_key  = "size",
  .open              = pattern_open,
  .get_size          = pattern_get_size,
  .can_multi_conn    = pattern_can_multi_conn,
  .can_cache         = pattern_can_cache,
  .pread             = pattern_pread,
  .pwrite            = pattern_pwrite,
  /* In this plugin, errno is preserved properly along error return
   * paths from failed system calls.
   */
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
