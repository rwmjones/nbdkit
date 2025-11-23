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
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <nbdkit-filter.h>

static const char *ro_file;     /* readonly-file, may be NULL */

static int
readonly_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
                 const char *key, const char *value)
{
  if (strcmp (key, "readonly-file") == 0) {
    if (ro_file != NULL) {
      nbdkit_error ("readonly-file parameter appears multiple times");
      return -1;
    }
    ro_file = value;
    return 0;
  }

  else
    return next (nxdata, key, value);
}

#define readonly_config_help \
  "readonly-file=FILENAME         If FILENAME present, set to readonly"

static void *
readonly_open (nbdkit_next_open *next, nbdkit_context *nxdata,
               int readonly, const char *exportname, int is_tls)
{
  /* If we're in "permanent readonly mode" then we set the readonly
   * flag for the underlying plugin, since that may make it behave
   * differently (or more efficiently).  We also return false for
   * can_write() below.  However if we're testing for the file then we
   * cannot do that.
   */
  if (ro_file == NULL)
    readonly = 1;
  if (next (nxdata, readonly, exportname) == -1)
    return NULL;
  return NBDKIT_HANDLE_NOT_NEEDED;
}

static int
readonly_can_write (nbdkit_next *next, void *handle)
{
  if (ro_file == NULL)
    return 0;
  /* We don't have to test if the file is present here, because if we
   * did that then the connection would be permanently read-only.
   */
  return next->can_write (next);
}

/* This function tests for readonly and injects the error if so.
 * Returns true if the operation should be rejected.
 */
static bool
is_readonly_mode (const char *fn, int *err)
{
  if (ro_file == NULL || access (ro_file, R_OK) == 0) {
    nbdkit_error ("%s operation rejected by readonly filter", fn);
    /* This is turned into NBD_EPERM in the server, but keep the more
     * descriptive errno in case the NBD protocol expands the range of
     * possible errors in future.  Also "Read-only file system" is
     * printed in the nbdkit log.
     */
    *err = EROFS;
    return true;
  }

  return false;
}

static int
readonly_pwrite (nbdkit_next *next,
                 void *handle,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err)
{
  if (is_readonly_mode ("pwrite", err)) return -1;
  return next->pwrite (next, buf, count, offset, flags, err);
}

static int
readonly_trim (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  if (is_readonly_mode ("trim", err)) return -1;
  return next->trim (next, count, offset, flags, err);
}

static int
readonly_zero (nbdkit_next *next,
               void *handle, uint32_t count, uint64_t offset,
               uint32_t flags, int *err)
{
  if (is_readonly_mode ("zero", err)) return -1;
  return next->zero (next, count, offset, flags, err);
}

/* Should we catch and deny flush requests?  Unclear, but an argument
 * can be made that we should not: Even if the disk has just been made
 * unwritable, any writes that were issued prior to that should be
 * allowed to flush, otherwise you could get inconsistency.  Also a
 * flush may not actually issue any writes, but it is hard to know
 * that from the filter.
 */

static struct nbdkit_filter filter = {
  .name              = "readonly",
  .longname          = "nbdkit readonly filter",
  .config            = readonly_config,
  .config_help       = readonly_config_help,
  .open              = readonly_open,
  .can_write         = readonly_can_write,
  .pwrite            = readonly_pwrite,
  .trim              = readonly_trim,
  .zero              = readonly_zero,
};

NBDKIT_REGISTER_FILTER (filter)
