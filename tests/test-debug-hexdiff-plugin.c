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

/* Plugin for testing nbdkit_debug_hexdiff.  See nbdkit-debug-hexdiff.sh */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

static const uint64_t size = 1024 * 1024;
static uint8_t data[1024 * 1024];

static void *
hexdiff_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int64_t
hexdiff_get_size (void *handle)
{
  return size;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static int
hexdiff_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  memcpy (buf, &data[offset], count);
  return 0;
}

static int
hexdiff_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  /* Show the differences between the existing data and what we are
   * about to write.
   */
  nbdkit_debug_hexdiff (&data[offset], buf, count, "DIFF: ", offset);
  /* Update the RAM disk. */
  memcpy (&data[offset], buf, count);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "hexdiff",
  .version           = PACKAGE_VERSION,
  .open              = hexdiff_open,
  .get_size          = hexdiff_get_size,
  .pread             = hexdiff_pread,
  .pwrite            = hexdiff_pwrite,
};

NBDKIT_REGISTER_PLUGIN (plugin)
