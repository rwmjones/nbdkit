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

/* This is the Windows version of the file plugin. */

#ifndef WIN32
#error "build error: file.c should be used on Unix-like platforms"
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <ws2tcpip.h>
#include <windows.h>

#include <pthread.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "isaligned.h"
#include "strndup.h"

static char *filename = NULL;

static void
winfile_unload (void)
{
  free (filename);
}

static int
winfile_config (const char *key, const char *value)
{
  if (strcmp (key, "file") == 0) {
    free (filename);
    filename = nbdkit_realpath (value);
    if (!filename)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the user passed the file parameter. */
static int
winfile_config_complete (void)
{
  if (!filename) {
    nbdkit_error ("you must supply either [file=]<FILENAME> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  return 0;
}

#define winfile_config_help \
  "[file=]<FILENAME>     The filename to serve."

/* Print some extra information about how the plugin was compiled. */
static void
winfile_dump_plugin (void)
{
  printf ("file_extents=yes\n");
  printf ("winfile=yes\n");
}

/* Return string corresponding to Windows error number.
 * https://learn.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
 * https://stackoverflow.com/questions/1387064/how-to-get-the-error-message-from-the-error-code-returned-by-getlasterror
 * XXX Maybe we should make nbdkit_error ("...%m") do the right thing?
 */
static char *
windows_error (DWORD err)
{
  LPSTR *msg = NULL;
  size_t len;
  char *ret;

  if (err == 0)
    ret = strdup ("no error");
  else {
    len = FormatMessageA (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                          FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS,
                          NULL, err,
                          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                          (LPSTR)&msg, 0, NULL);
    ret = strndup ((char *) msg, len);
    LocalFree (msg);
  }

  return ret;
}

/* Per-connection handle. */
struct handle {
  HANDLE fh;
  int64_t size;
  bool is_readonly;
  bool is_volume;
  bool is_sparse;
  uint16_t blocksize; /* 1 for files, sector_size (512 or 4096) for volumes */
};

static void *
winfile_open (int readonly)
{
  struct handle *h;
  HANDLE fh;
  LARGE_INTEGER size = { 0 };
  DWORD flags;
  bool is_volume;
  BY_HANDLE_FILE_INFORMATION fileinfo;
  bool is_sparse;
  uint16_t blocksize = 1;

  flags = GENERIC_READ;
  if (!readonly) flags |= GENERIC_WRITE;

  fh = CreateFile (filename, flags, FILE_SHARE_READ|FILE_SHARE_WRITE,
                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh == INVALID_HANDLE_VALUE && !readonly) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_debug ("open for writing failed, falling back to read-only: "
                  "%s: %lu: %s",
                  filename, err, msg);

    flags &= ~GENERIC_WRITE;
    readonly = true;
    fh = CreateFile (filename, flags, FILE_SHARE_READ|FILE_SHARE_WRITE,
                     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  }
  if (fh == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_error ("%s: CreateFile: %lu: %s", filename, err, msg);
    return NULL;
  }

  /* https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file#win32-device-namespaces */
  is_volume = strncmp (filename, "\\\\.\\", 4) == 0;

  if (is_volume) {
    /* Windows volume (block device).  Get the size. */
    GET_LENGTH_INFORMATION li;
    DISK_GEOMETRY dg;
    DWORD lisz = sizeof li;
    DWORD obsz = 0;

    if (!DeviceIoControl (fh, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                          (LPVOID) &li, lisz, &obsz, NULL)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: DeviceIoControl: IOCTL_DISK_GET_LENGTH_INFO: %lu: %s",
                    filename, err, msg);
      CloseHandle (fh);
      return NULL;
    }
    size.QuadPart = li.Length.QuadPart;

    if (!DeviceIoControl (fh, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                          (LPVOID) &dg, sizeof dg, &obsz, NULL)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: DeviceIoControl: IOCTL_DISK_GET_DRIVE_GEOMETRY: "
                    "%lu: %s",
                    filename, err, msg);
      CloseHandle (fh);
      return NULL;
    }
    blocksize = dg.BytesPerSector;
  }
  else {
    /* Regular file.  Get the size. */
    if (!GetFileSizeEx (fh, &size)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: GetFileSizeEx: %lu: %s", filename, err, msg);
      CloseHandle (fh);
      return NULL;
    }
  }

  /* Sparseness is a file property in Windows.  Whoever creates the
   * file must set the property, we won't modify it.  However we must
   * see if the file is sparse and enable trimming if so.
   *
   * I couldn't find out how to handle sparse volumes, so if the call
   * below fails assume non-sparse.
   *
   * https://docs.microsoft.com/en-us/windows/win32/fileio/sparse-file-operations
   * http://www.flexhex.com/docs/articles/sparse-files.phtml
   */
  is_sparse = false;
  if (GetFileInformationByHandle (fh, &fileinfo))
    is_sparse = fileinfo.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE;

  h = malloc (sizeof *h);
  if (!h) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_error ("malloc: %lu: %s", err, msg);
    CloseHandle (fh);
    return NULL;
  }
  h->fh = fh;
  h->size = size.QuadPart;
  h->is_readonly = readonly;
  h->is_volume = is_volume;
  h->is_sparse = is_sparse;
  h->blocksize = blocksize;
  nbdkit_debug ("%s: size=%" PRIi64 " readonly=%s is_volume=%s is_sparse=%s blocksize=%d",
                filename, h->size,
                readonly ? "true" : "false",
                is_volume ? "true" : "false",
                is_sparse ? "true" : "false",
                blocksize);
  return h;
}

static int
winfile_can_write (void *handle)
{
  struct handle *h = handle;
  return !h->is_readonly;
}

/* Windows cannot flush on a read-only file.  It returns
 * ERROR_ACCESS_DENIED.  Therefore don't advertise flush if the handle
 * is r/o.
 */
static int
winfile_can_flush (void *handle)
{
  struct handle *h = handle;
  return !h->is_readonly;
}

static int
winfile_can_trim (void *handle)
{
  struct handle *h = handle;
  return h->is_sparse;
}

static int
winfile_can_zero (void *handle)
{
  return 1;
}

static int
winfile_can_extents (void *handle)
{
  struct handle *h = handle;
  return h->is_sparse;
}

static void
winfile_close (void *handle)
{
  struct handle *h = handle;
  CloseHandle (h->fh);
}

static int64_t
winfile_get_size (void *handle)
{
  struct handle *h = handle;
  return h->size;
}

static int
winfile_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  struct handle *h = handle;
  DWORD r;
  OVERLAPPED ovl;

  memset (&ovl, 0, sizeof ovl);
  ovl.Offset = offset & 0xffffffff;
  ovl.OffsetHigh = offset >> 32;

  /* XXX Will fail weirdly if count is larger than 32 bits. */
  if (!ReadFile (h->fh, buf, count, &r, &ovl)) {
    DWORD lasterror = GetLastError();
    CLEANUP_FREE char *msg = windows_error (lasterror);

    nbdkit_error ("%s: ReadFile: %lu: %s (offset: %llx count: %d",
                  filename, lasterror, msg, offset, count);
    if (lasterror == ERROR_INVALID_PARAMETER &&
        h->blocksize == 4096 &&
        (! IS_ALIGNED (offset, 4096) || ! IS_ALIGNED (count, 4096))) {
      nbdkit_error ("%s: ReadFile: possible unaligned read on 4k raw device, "
                    "consider --filter=blocksize",
                    filename);
    }
    return -1;
  }
  return 0;
}

static int
winfile_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  struct handle *h = handle;
  DWORD r;
  OVERLAPPED ovl;

  memset (&ovl, 0, sizeof ovl);
  ovl.Offset = offset & 0xffffffff;
  ovl.OffsetHigh = offset >> 32;

  /* XXX Will fail weirdly if count is larger than 32 bits. */
  if (!WriteFile (h->fh, buf, count, &r, &ovl)) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_error ("%s: WriteFile: %lu: %s", filename, err, msg);
    return -1;
  }

  if (flags & NBDKIT_FLAG_FUA) {
    if (!FlushFileBuffers (h->fh)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: FlushFileBuffers: %lu: %s", filename, err, msg);
      return -1;
    }
  }

  return 0;
}

static int
winfile_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;

  if (!FlushFileBuffers (h->fh)) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_error ("%s: FlushFileBuffers: %lu: %s", filename, err, msg);
    return -1;
  }

  return 0;
}

static int
winfile_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  FILE_ZERO_DATA_INFORMATION info;
  DWORD t;

  assert (h->is_sparse);

  info.FileOffset.QuadPart = offset;
  info.BeyondFinalZero.QuadPart = offset + count;
  if (!DeviceIoControl (h->fh, FSCTL_SET_ZERO_DATA, &info, sizeof info,
                        NULL, 0, &t, NULL)) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = windows_error (err);

    nbdkit_error ("%s: DeviceIoControl: FSCTL_SET_ZERO_DATA: %lu: %s",
                  filename, err, msg);
    return -1;
  }

  if (flags & NBDKIT_FLAG_FUA) {
    if (!FlushFileBuffers (h->fh)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: FlushFileBuffers: %lu", filename, err, msg);
      return -1;
    }
  }

  return 0;
}

static int
winfile_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  FILE_ZERO_DATA_INFORMATION info;
  DWORD t;

  /* This is documented to work for both non-sparse and sparse files,
   * but for sparse files it creates a hole.  If the file is sparse
   * and !NBDKIT_FLAG_MAY_TRIM then we should fall back to writing
   * zeros (by returning errno ENOTSUP).  Also I found that Wine does
   * not support this call, so in that case we also turn the Windows
   * error ERROR_NOT_SUPPORTED into ENOTSUP.
   */
  if (h->is_sparse && (flags & NBDKIT_FLAG_MAY_TRIM) == 0) {
    errno = ENOTSUP;
    return -1;
  }
  info.FileOffset.QuadPart = offset;
  info.BeyondFinalZero.QuadPart = offset + count;
  if (!DeviceIoControl (h->fh, FSCTL_SET_ZERO_DATA, &info, sizeof info,
                        NULL, 0, &t, NULL)) {
    DWORD err = GetLastError ();
    CLEANUP_FREE char *msg = NULL;

    if (err == ERROR_NOT_SUPPORTED) {
      errno = ENOTSUP;
      return -1;
    }

    msg = windows_error (err);
    nbdkit_error ("%s: DeviceIoControl: FSCTL_SET_ZERO_DATA: %lu: %s",
                  filename, err, msg);
    return -1;
  }

  if (flags & NBDKIT_FLAG_FUA) {
    if (!FlushFileBuffers (h->fh)) {
      DWORD err = GetLastError ();
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: FlushFileBuffers: %lu: %s", filename, err, msg);
      return -1;
    }
  }

  return 0;
}

static int
winfile_extents (void *handle, uint32_t count, uint64_t offset,
                 uint32_t flags, struct nbdkit_extents *extents)
{
  struct handle *h = handle;
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  FILE_ALLOCATED_RANGE_BUFFER query;
  FILE_ALLOCATED_RANGE_BUFFER ranges[16];
  DWORD nb, n, i, err;
  BOOL r;
  uint64_t last_offset = offset, this_offset, this_length;

  query.FileOffset.QuadPart = offset;
  query.Length.QuadPart = count;

  do {
    r = DeviceIoControl (h->fh, FSCTL_QUERY_ALLOCATED_RANGES,
                         &query, sizeof query, ranges, sizeof ranges,
                         &nb, NULL);
    err = GetLastError ();

    /* This can return an error with ERROR_MORE_DATA which is not
     * really an error, it means there is more data to be fetched
     * after the set of ranges returned in this call.
     */
    if (!r && err != ERROR_MORE_DATA) {
      CLEANUP_FREE char *msg = windows_error (err);

      nbdkit_error ("%s: DeviceIoControl: FSCTL_QUERY_ALLOCATED_RANGES: "
                    "%lu: %s",
                    filename, err, msg);
      return -1;
    }

    /* Number of ranges returned in this call. */
    n = nb / sizeof ranges[0];

    for (i = 0; i < n; ++i) {
      this_offset = ranges[i].FileOffset.QuadPart;
      this_length = ranges[i].Length.QuadPart;

      /* The call returns only allocated ranges, so we must insert
       * holes between them.  Holes always read back as zero.
       */
      if (last_offset < this_offset) {
        if (nbdkit_add_extent (extents, last_offset, this_offset-last_offset,
                               NBDKIT_EXTENT_HOLE|NBDKIT_EXTENT_ZERO) == -1)
          return -1;
      }
      if (nbdkit_add_extent (extents, this_offset, this_length, 0) == -1)
        return -1;
      last_offset = this_offset + this_length;

      if (req_one)
        return 0;
    }
  } while (!r /* && err == ERROR_MORE_DATA (implied by error test above) */);

  return 0;
}

static int
winfile_block_size (void *handle,
                   uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  struct handle *h = handle;
  if (h->blocksize == 1) {
    *minimum = *preferred = *maximum = 0;
  } else {
    *minimum = h->blocksize;
    /* minimum above is what's essential.  Set the others to sane defaults */
    *preferred = 32 * 1024;
    *maximum = 32 * 1024 * 1024;
  }
  return 0;
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

static struct nbdkit_plugin plugin = {
  .name              = "file",
  .longname          = "nbdkit file plugin (Windows)",
  .version           = PACKAGE_VERSION,

  .unload            = winfile_unload,

  .config            = winfile_config,
  .config_complete   = winfile_config_complete,
  .config_help       = winfile_config_help,
  .magic_config_key  = "file",
  .dump_plugin       = winfile_dump_plugin,

  .open              = winfile_open,
  .can_write         = winfile_can_write,
  .can_flush         = winfile_can_flush,
  .can_trim          = winfile_can_trim,
  .can_zero          = winfile_can_zero,
  .can_extents       = winfile_can_extents,
  .close             = winfile_close,
  .get_size          = winfile_get_size,
  .block_size        = winfile_block_size,
  .pread             = winfile_pread,
  .pwrite            = winfile_pwrite,
  .flush             = winfile_flush,
  .trim              = winfile_trim,
  .zero              = winfile_zero,
  .extents           = winfile_extents,

  .errno_is_preserved = 1, /* XXX ? */
};

NBDKIT_REGISTER_PLUGIN (plugin)
