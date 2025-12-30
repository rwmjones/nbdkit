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

/* Plugin for testing nbdkit_debug_hexdump.  See nbdkit-debug-hexdump.sh */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

static void *
hexdump_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int64_t
hexdump_get_size (void *handle)
{
  return INT64_MAX;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Never actually called, but return zeroes. */
static int
hexdump_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  memset (buf, 0, count);
  return 0;
}

/* We hexdump the buffer, with a prefix string, and discard the data. */
static int
hexdump_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  nbdkit_debug_hexdump (buf, count, "DUMP: ", offset);
  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "hexdump",
  .version           = PACKAGE_VERSION,
  .open              = hexdump_open,
  .get_size          = hexdump_get_size,
  .pread             = hexdump_pread,
  .pwrite            = hexdump_pwrite,
};

NBDKIT_REGISTER_PLUGIN (plugin)
