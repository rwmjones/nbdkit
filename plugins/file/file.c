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

#ifdef WIN32
#error "build error: winfile.c should be used on Windows"
#endif

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <pthread.h>

#if defined (__linux__) && !defined (FALLOC_FL_PUNCH_HOLE)
#include <linux/falloc.h>   /* For FALLOC_FL_*, glibc < 2.18 */
#endif

#if defined (__linux__) && HAVE_LINUX_FS_H
#include <linux/fs.h>       /* For BLK* constants. */
#endif

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "cleanup.h"
#include "fdatasync.h"
#include "isaligned.h"
#include "ispowerof2.h"
#include "minmax.h"
#include "once.h"
#include "utils.h"

static enum {
  mode_none,
  mode_filename,
  mode_directory,
  mode_fd,
  mode_dirfd,
} mode = mode_none;
static char *filename = NULL;
static char *directory = NULL;
static int filedesc = -1;

/* posix_fadvise mode: -1 = don't set it, or POSIX_FADV_*. */
static int fadvise_mode =
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_NORMAL)
  POSIX_FADV_NORMAL
#else
  -1
#endif
  ;

/* Reduce memory pressure. */
static bool reduce_memory_pressure = false;

/* Define EVICT_WRITES if we are going to evict the page cache
 * (reduce-memory-pressure=true) after writing.  This is only known to
 * work on Linux.
 */
#ifdef __linux__
#define EVICT_WRITES 1
#endif

#ifdef EVICT_WRITES
/* Queue writes so they will be evicted from the cache.  See
 * libnbd.git copy/file-ops.c for the rationale behind this.
 */
#define NR_WINDOWS 8

struct write_window {
  int fd;
  uint64_t offset;
  size_t len;           /* window slot only valid if len > 0 */
};

static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t window_fd_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct write_window window[NR_WINDOWS];

static int
evict_writes (int fd, uint64_t offset, size_t len)
{
  ACQUIRE_RDLOCK_FOR_CURRENT_SCOPE (&window_fd_lock);
  struct write_window oldest = { 0 };

  {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&window_lock);

    /* Save oldest window[0] for eviction below, and move all windows
     * down one.  Set the newest slot to empty.
     */
    oldest = window[0];
    memmove (&window[0], &window[1], sizeof window[0] * (NR_WINDOWS-1));
    window[NR_WINDOWS-1].len = 0;

    /* Tell Linux to start writing the current range out to disk
     * (asynchronously).
     */
    if (sync_file_range (fd, offset, len, SYNC_FILE_RANGE_WRITE) == -1) {
      nbdkit_error ("sync_file_range: reduce-memory-pressure: "
                    "starting eviction: %m");
      return -1;
    }

    /* Add the range to the newest end of the list of windows. */
    window[NR_WINDOWS-1].fd = fd;
    window[NR_WINDOWS-1].offset = offset;
    window[NR_WINDOWS-1].len = len;
  }
  /* Release window_lock here, but hold window_fd_lock since we don't
   * want file_close -> remove_fd_from_window to close oldest.fd
   */

  /* Evict the oldest window from the page cache (synchronously). */
  if (oldest.len > 0) {
    int r;

    if (sync_file_range (oldest.fd, oldest.offset, oldest.len,
                         SYNC_FILE_RANGE_WAIT_BEFORE |
                         SYNC_FILE_RANGE_WRITE |
                         SYNC_FILE_RANGE_WAIT_AFTER) == -1) {
      nbdkit_error ("sync_file_range: reduce-memory-pressure: "
                    "evicting oldest window: %m");
      return -1;
    }
    r = posix_fadvise (oldest.fd, oldest.offset, oldest.len,
                       POSIX_FADV_DONTNEED);
    if (r != 0) {
      errno = r;
      nbdkit_debug ("posix_fadvise: POSIX_FADV_DONTNEED: (ignored): %m");
    }
  }

  return 0;
}

/* When we close the handle we must remove any windows which are still
 * associated.  They missed the boat, oh well :-(
 */
static void
remove_fd_from_window (int fd)
{
  ACQUIRE_WRLOCK_FOR_CURRENT_SCOPE (&window_fd_lock);
  size_t i;

  for (i = 0; i < NR_WINDOWS; ++i)
    if (window[i].len > 0 && window[i].fd == fd)
      window[i].len = 0;
}
#endif /* EVICT_WRITES */

/* Any callbacks using lseek must be protected by this lock. */
static pthread_mutex_t lseek_lock = PTHREAD_MUTEX_INITIALIZER;

/* to enable: -D file.zero=1 */
NBDKIT_DLL_PUBLIC int file_debug_zero;

static bool __attribute__ ((unused))
is_enotsup (int err)
{
  return err == ENOTSUP || err == EOPNOTSUPP;
}

static void
file_unload (void)
{
  free (filename);
  free (directory);
}

/* Called for each key=value passed on the command line. */
static int
file_config (const char *key, const char *value)
{
  /* Our use of nbdkit_realpath requires the destination to exist at
   * startup; use nbdkit_absolute_path instead if we wanted to defer
   * existence checks to the last possible moment.
   */
  if (strcmp (key, "file") == 0) {
    if (mode != mode_none) goto wrong_mode;
    mode = mode_filename;
    assert (filename == NULL);
    filename = nbdkit_realpath (value);
    if (!filename)
      return -1;
  }
  else if (strcmp (key, "directory") == 0 ||
           strcmp (key, "dir") == 0) {
    if (mode != mode_none) goto wrong_mode;
    mode = mode_directory;
    assert (directory == NULL);
    directory = nbdkit_realpath (value);
    if (!directory)
      return -1;
  }
  else if (strcmp (key, "fd") == 0) {
    if (mode != mode_none) goto wrong_mode;
    mode = mode_fd;
    assert (filedesc == -1);
    if (nbdkit_parse_int ("fd", value, &filedesc) == -1)
      return -1;
    if (filedesc <= STDERR_FILENO) {
      nbdkit_error ("file descriptor must be > %d because "
                    "stdin, stdout and stderr are reserved for nbdkit",
                    STDERR_FILENO);
      return -1;
    }
  }
  else if (strcmp (key, "dirfd") == 0) {
    if (mode != mode_none) goto wrong_mode;
    mode = mode_dirfd;
    assert (filedesc == -1);
    if (nbdkit_parse_int ("dirfd", value, &filedesc) == -1)
      return -1;
    if (filedesc <= STDERR_FILENO) {
      nbdkit_error ("file descriptor must be > %d because "
                    "stdin, stdout and stderr are reserved for nbdkit",
                    STDERR_FILENO);
      return -1;
    }
  }
  else if (strcmp (key, "fadvise") == 0) {
    /* As this is a hint, if the kernel doesn't support the feature
     * ignore the parameter.
     */
    if (strcmp (value, "normal") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_NORMAL)
      fadvise_mode = POSIX_FADV_NORMAL;
#else
      fadvise_mode = -1;
#endif
    }
    else if (strcmp (value, "random") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_RANDOM)
      fadvise_mode = POSIX_FADV_RANDOM;
#else
      fadvise_mode = -1;
#endif
    }
    else if (strcmp (value, "sequential") == 0) {
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_SEQUENTIAL)
      fadvise_mode = POSIX_FADV_SEQUENTIAL;
#else
      fadvise_mode = -1;
#endif
    }
    else {
      nbdkit_error ("unknown fadvise mode: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "reduce-memory-pressure") == 0) {
    int r;

    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    reduce_memory_pressure = r;
  }
  /* For compatibility with nbdkit < 1.46 */
  else if (strcmp (key, "cache") == 0) {
    if (strcmp (value, "default") == 0)
      reduce_memory_pressure = false;
    else if (strcmp (value, "none") == 0)
      reduce_memory_pressure = true;
    else {
      nbdkit_error ("unknown cache mode: %s", value);
      return -1;
    }
  }
  else if (strcmp (key, "rdelay") == 0 ||
           strcmp (key, "wdelay") == 0) {
    nbdkit_error ("add --filter=delay on the command line");
    return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }
  return 0;

 wrong_mode:
  nbdkit_error ("%s parameter can only appear once on the command line",
                "file|dir|fd|dirfd");
  return -1;
}

/* Check that the user passed exactly one parameter. */
static int
file_config_complete (void)
{
  int r;
  struct stat sb;

  switch (mode) {
  case mode_none:
    nbdkit_error ("you must supply [file=]<FILENAME>, "
                  "dir=<DIRNAME>, fd=<FD> or dirfd=<FD> "
                  "parameter after the plugin name "
                  "on the command line");
    return -1;

  case mode_filename:
    assert (filename != NULL);
    assert (directory == NULL);
    assert (filedesc == -1);

    /* Sanity check now, rather than waiting for first client open.
     * See also comment in .config about use of nbdkit_realpath.  Yes,
     * this is a harmless TOCTTOU race.
     */
    r = stat (filename, &sb);
    if (r == 0 && S_ISDIR (sb.st_mode)) {
      nbdkit_error ("use dir= to serve files within %s", filename);
      return -1;
    }
    if (r == -1 || !(S_ISBLK (sb.st_mode) || S_ISREG (sb.st_mode))) {
      nbdkit_error ("file is not regular or block device: %s", filename);
      return -1;
    }
    break;

  case mode_directory:
    assert (filename == NULL);
    assert (directory != NULL);
    assert (filedesc == -1);

    if (stat (directory, &sb) == -1 || !S_ISDIR (sb.st_mode)) {
      nbdkit_error ("expecting a directory: %s", directory);
      return -1;
    }
    break;

  case mode_fd:
    assert (filename == NULL);
    assert (directory == NULL);
    assert (filedesc > STDERR_FILENO);

    r = fstat (filedesc, &sb);
    if (r == -1 || !(S_ISBLK (sb.st_mode) || S_ISREG (sb.st_mode))) {
      nbdkit_error ("fd is not regular or block device: %d", filedesc);
      return -1;
    }
    break;

  case mode_dirfd:
    assert (filename == NULL);
    assert (directory == NULL);
    assert (filedesc > STDERR_FILENO);

    r = fstat (filedesc, &sb);
    if (r == -1 || !(S_ISDIR (sb.st_mode))) {
      nbdkit_error ("dirfd is not a directory: %d", filedesc);
      return -1;
    }
  }

  return 0;
}

#define file_config_help \
  "[file=]<FILENAME>       The filename to serve\n" \
  "fd=<FILE_DESCRIPTOR>    Serve file attached to file descriptor\n" \
  "dir=<DIRNAME>           A directory containing files to serve\n" \
  "dirfd=<FILE_DESCRIPTOR> Serve dir attached to file descriptor\n" \
  "reduce-memory-pressure=true  Reduce use of page cache (default: false)\n" \
  "fadvise=<LEVEL>         Set fadvise hint (normal, random, sequential)"

/* Print some extra information about how the plugin was compiled. */
static void
file_dump_plugin (void)
{
#if defined(BLKSSZGET) && defined(BLKIOMIN) && defined(BLKIOOPT)
  printf ("file_block_size=yes\n");
#endif
#ifdef BLKROTATIONAL
  printf ("file_blkrotational=yes\n");
#endif
#ifdef BLKSSZGET
  printf ("file_blksszget=yes\n");
#endif
#ifdef BLKDISCARD
  printf ("file_blkdiscard=yes\n");
#endif
#ifdef BLKZEROOUT
  printf ("file_blkzeroout=yes\n");
#endif
#ifdef SEEK_HOLE
  printf ("file_extents=yes\n");
#endif
#ifdef FALLOC_FL_PUNCH_HOLE
  printf ("file_falloc_fl_punch_hole=yes\n");
#endif
#ifdef FALLOC_FL_ZERO_RANGE
  printf ("file_falloc_fl_zero_range=yes\n");
#endif
}

/* Common code for listing exports of a directory. */
static int
list_exports_of_directory (struct nbdkit_exports *exports, DIR *dir)
{
  struct dirent *entry;

  errno = 0;
  while ((entry = readdir (dir)) != NULL) {
    int r = -1;
    struct stat sb;

#if HAVE_STRUCT_DIRENT_D_TYPE
    if (entry->d_type == DT_BLK || entry->d_type == DT_REG)
      r = 1;
    else if (entry->d_type != DT_LNK && entry->d_type != DT_UNKNOWN)
      r = 0;
#endif
    /* TODO: when chasing symlinks, is statx any nicer than fstatat? */
    if (r == -1 && fstatat (dirfd (dir), entry->d_name, &sb, 0) == 0 &&
        (S_ISREG (sb.st_mode) || S_ISBLK (sb.st_mode)))
      r = 1;
    if (r == 1 && nbdkit_add_export (exports, entry->d_name, NULL) == -1)
      return -1;
    errno = 0;
  }

  if (errno) {
    nbdkit_error ("readdir: %m");
    return -1;
  }

  return 0;
}

static int
file_list_exports (int readonly, int default_only,
                   struct nbdkit_exports *exports)
{
  /* We don't fork, so no need to worry about FD_CLOEXEC on the directory */
  DIR *dir;
  int dfd, r;

  switch (mode) {
  case mode_filename:
  case mode_fd:
    return nbdkit_add_export (exports, "", NULL);

  case mode_directory:
    dir = opendir (directory);
    if (dir == NULL) {
      nbdkit_error ("opendir: %m");
      return -1;
    }
    r = list_exports_of_directory (exports, dir);
    closedir (dir);
    return r;

  case mode_dirfd:
    dfd = dup (filedesc);
    if (dfd == -1) {
      nbdkit_error ("dup: %m");
      return -1;
    }
    dir = fdopendir (dfd);
    if (dir == NULL) {
      nbdkit_error ("fdopendir: %m");
      return -1;
    }
    r = list_exports_of_directory (exports, dir);
    closedir (dir); /* also closes dfd */
    return r;

  default: abort ();
  }
}

/* The per-connection handle. */
struct handle {
  int fd;
  char *name;                   /* Filename or equivalent. */
  struct stat statbuf;
  bool is_block_device;
  int sector_size;
  unsigned short rotational;
  uint32_t minimum, preferred, maximum;
  bool can_write;
  bool can_punch_hole;
  bool can_zero_range;
  bool can_fallocate;
  bool can_blkdiscard;
  bool can_blkzeroout;
};

/* Common code for opening a file by name, used by mode_filename and
 * mode_directory only.  If successful, sets h->fd and may adjust
 * h->can_write.
 */
static int
open_file_by_name (struct handle *h, int readonly, int dfd, const char *file)
{
  int flags;

  assert (h->fd == -1);

  if (file[0] == '\0') {
    nbdkit_error ("open: cannot use empty file name or export name (\"\")");
    errno = ENOENT;
    return -1;
  }

  flags = O_CLOEXEC|O_NOCTTY;
  if (readonly)
    flags |= O_RDONLY;
  else
    flags |= O_RDWR;

  h->fd = openat (dfd, file, flags);
  if (h->fd == -1 && !readonly) {
    nbdkit_debug ("open O_RDWR failed, falling back to read-only: %s: %m",
                  file);
    flags = (flags & ~O_ACCMODE) | O_RDONLY;
    h->fd = openat (dfd, file, flags);
    h->can_write = false;
  }
  if (h->fd == -1) {
    nbdkit_error ("open: %s: %m", file);
    return -1;
  }

  return 0;
}

/* Create the per-connection handle. */
static void *
file_open (int readonly)
{
  struct handle *h;

  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    return NULL;
  }
  h->can_write = !readonly;
  h->fd = -1;
  h->name = NULL;

  switch (mode) {
  case mode_filename:
    h->name = strdup (filename);
    if (h->name == NULL) {
      nbdkit_error ("strdup: %m");
      goto err;
    }
    if (open_file_by_name (h, readonly, -1, h->name) == -1)
      goto err;
    break;

  case mode_directory: {
    int dfd;
    const char *export_name = nbdkit_export_name ();

    if (export_name == NULL)
      goto err;

    h->name = strdup (export_name);
    if (h->name == NULL) {
      nbdkit_error ("strdup: %m");
      goto err;
    }
    if (strchr (h->name, '/')) {
      nbdkit_error ("%s: exportname cannot contain /", h->name);
      errno = EINVAL;
      goto err;
    }
    dfd = open (directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd == -1) {
      nbdkit_error ("open %s: %m", directory);
      goto err;
    }
    if (open_file_by_name (h, readonly, dfd, h->name) == -1) {
      close (dfd);
      goto err;
    }
    close (dfd);
    break;
  }

  case mode_fd: {
    int r;

    if (asprintf (&h->name, "fd=%d", filedesc) == -1) {
      nbdkit_error ("asprintf: %m");
      goto err;
    }
    h->fd = dup (filedesc);
    if (h->fd == -1) {
      nbdkit_error ("dup: %s: %m", h->name);
      goto err;
    }

    /* If the file descriptor is readonly then we should not advertise
     * writes as they will fail later.
     */
    r = fcntl (h->fd, F_GETFL);
    if (r == -1) {
      nbdkit_error ("fcntl: %s: F_GETFL: %m", h->name);
      goto err;
    }
    r &= O_ACCMODE;
    if (r == O_RDONLY)
      h->can_write = false;
    else if (r == O_WRONLY)
      nbdkit_debug ("file descriptor is write-only (ie. not readable): "
                    "NBD protocol does not support this, but continuing "
                    "anyway!");
    break;
  }

  case mode_dirfd: {
    int dfd;
    const char *export_name = nbdkit_export_name ();

    if (export_name == NULL)
      goto err;

    h->name = strdup (export_name);
    if (h->name == NULL) {
      nbdkit_error ("strdup: %m");
      goto err;
    }
    if (strchr (h->name, '/')) {
      nbdkit_error ("%s: exportname cannot contain /", h->name);
      errno = EINVAL;
      goto err;
    }
    /* We don't fork, so no need to worry about FD_CLOEXEC on the directory */
    dfd = dup (filedesc);
    if (dfd == -1) {
      nbdkit_error ("dup: dirfd=%d: %m", filedesc);
      goto err;
    }
    if (open_file_by_name (h, readonly, dfd, h->name) == -1) {
      close (dfd);
      goto err;
    }
    close (dfd);
    break;
  }

  default:
    abort ();
  }

  assert (h->fd >= 0);
  assert (h->name != NULL);

  if (fstat (h->fd, &h->statbuf) == -1) {
    nbdkit_error ("fstat: %s: %m", h->name);
    goto err;
  }

  if (fadvise_mode != -1) {
    /* This is a hint so we ignore failures. */
#ifdef HAVE_POSIX_FADVISE
    int r = posix_fadvise (h->fd, 0, 0, fadvise_mode);
    if (r != 0) {
      errno = r;
      nbdkit_debug ("posix_fadvise: %s: (ignored): %m", h->name);
    }
#else
    nbdkit_debug ("fadvise is not supported");
#endif
  }

  if (S_ISBLK (h->statbuf.st_mode))
    h->is_block_device = true;
  else if (S_ISREG (h->statbuf.st_mode))
    h->is_block_device = false;
  else {
    nbdkit_error ("file is not regular or block device: %s", h->name);
    goto err;
  }

  h->sector_size = 4096; /* Start with safe guess */
#ifdef BLKSSZGET
  if (h->is_block_device) {
    if (ioctl (h->fd, BLKSSZGET, &h->sector_size) == -1)
      nbdkit_debug ("cannot get sector size: %s: %m", h->name);
  }
#endif

  h->rotational = 0; /* Default before nbdkit 1.40 */
#ifdef BLKROTATIONAL
  if (h->is_block_device) {
    if (ioctl (h->fd, BLKROTATIONAL, &h->rotational) == -1)
      nbdkit_debug ("cannot get rotational property: %s: %m", h->name);
  }
#endif

  h->minimum = h->preferred = h->maximum = 0;
#if defined(BLKSSZGET) && defined(BLKIOMIN) && defined(BLKIOOPT)
  if (h->is_block_device) {
    int logical_block_size = 0;
    unsigned minimum_io_size = 0, optimal_io_size = 0;

    if (ioctl (h->fd, BLKSSZGET, &logical_block_size) == -1)
      nbdkit_debug ("ioctl: %s: %s: %m", "BLKSSZGET", h->name);

    if (ioctl (h->fd, BLKIOMIN, &minimum_io_size) == -1)
      nbdkit_debug ("ioctl: %s: %s: %m", "BLKIOMIN", h->name);

    if (ioctl (h->fd, BLKIOOPT, &optimal_io_size) == -1)
      nbdkit_debug ("ioctl: %s: %s: %m", "BLKIOOPT", h->name);
    else if (optimal_io_size == 0)
      /* All devices in the Linux kernel except for MD report optimal
       * as 0.  In that case use logical_block_size.
       */
      optimal_io_size = logical_block_size;

    /* Check the values are sane before using them. */
    if (logical_block_size >= 512 && is_power_of_2 (logical_block_size) &&
        minimum_io_size >= 512 && is_power_of_2 (minimum_io_size) &&
        minimum_io_size >= logical_block_size &&
        optimal_io_size >= 512 && is_power_of_2 (optimal_io_size) &&
        optimal_io_size >= logical_block_size) {
      /* The mapping from Linux kernel settings to NBD protocol block
       * sizes is not obvious.  logical_block_size is the sector size,
       * and anything smaller than this will cause a RMW cycle.  For
       * the preferred size, we are advised to use the largest of
       * minimum_io_size and optimal_io_size.  For this plugin maximum
       * can be the largest that NBD can handle.
       */

      /* 64K is the largest minimum that the NBD protocol supports. */
      h->minimum = MIN (65536, logical_block_size);

      h->preferred = MAX (minimum_io_size, optimal_io_size);
      h->maximum = 0xffffffff;
    }
  }
#endif

#ifdef FALLOC_FL_PUNCH_HOLE
  h->can_punch_hole = true;
#else
  h->can_punch_hole = false;
#endif

#ifdef FALLOC_FL_ZERO_RANGE
  h->can_zero_range = true;
#else
  h->can_zero_range = false;
#endif

  h->can_fallocate = true;
  h->can_blkzeroout = h->is_block_device;
  h->can_blkdiscard = h->is_block_device;

  nbdkit_debug ("%s: fd=%d mode=%o "
                "%s%s%s%s%s%s%s"
                "sector_size=%d rotational=%s "
                "blocksizes=(%" PRIu32 ",%" PRIu32 ",%" PRIu32 ")",
                h->name, h->fd, h->statbuf.st_mode,
                h->is_block_device ? "block_device " : "",
                h->can_write       ? "can_write " : "readonly ",
                h->can_punch_hole  ? "can_punch_hole " : "",
                h->can_zero_range  ? "can_zero_range " : "",
                h->can_fallocate   ? "can_fallocate " : "",
                h->can_blkdiscard  ? "can_blkdiscard " : "",
                h->can_blkzeroout  ? "can_blkzeroout " : "",
                h->sector_size, h->rotational ? "true" : "false",
                h->minimum, h->preferred, h->maximum);

  return h;

 err:
  if (h->fd >= 0)
    close (h->fd);
  free (h->name);
  free (h);
  return NULL;
}

/* Free up the per-connection handle. */
static void
file_close (void *handle)
{
  struct handle *h = handle;

#ifdef EVICT_WRITES
  remove_fd_from_window (h->fd);
#endif
  close (h->fd);
  free (h->name);
  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Get the file size. */
static int64_t
file_get_size (void *handle)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock); /* device_size may seek */
  struct handle *h = handle;
  int64_t r;

  r = device_size (h->fd, &h->statbuf);
  if (r == -1) {
    nbdkit_error ("device_size: %s: %m", h->name);
    return -1;
  }
  return r;
}

/* Check if file is rotational. */
static int
file_is_rotational (void *handle)
{
  struct handle *h = handle;

  return h->rotational;
}

/* Return minimum, preferred and maximum block size. */
static int
file_block_size (void *handle, uint32_t *minimum,
                 uint32_t *preferred, uint32_t *maximum)
{
  struct handle *h = handle;

  *minimum = h->minimum;
  *preferred = h->preferred;
  *maximum = h->maximum;
  return 0;
}

/* Check if file is read-only. */
static int
file_can_write (void *handle)
{
  struct handle *h = handle;

  return h->can_write;
}

/* Allow multiple parallel connections from a single client. */
static int
file_can_multi_conn (void *handle)
{
  return 1;
}

static int
file_can_trim (void *handle)
{
  /* Trim is advisory, but we prefer to advertise it only when we can
   * actually (attempt to) punch holes.  Since not all filesystems
   * support all fallocate modes, it would be nice if we had a way
   * from fpathconf() to definitively learn what will work on a given
   * fd for a more precise answer; oh well.  */
#ifdef FALLOC_FL_PUNCH_HOLE
  return 1;
#else
  return 0;
#endif
}

static int
file_can_fua (void *handle)
{
  return NBDKIT_FUA_NATIVE;
}

static int
file_can_cache (void *handle)
{
  /* Prefer posix_fadvise(), but letting nbdkit call .pread on our
   * behalf also tends to work well for the local file system
   * cache.
   */
#if HAVE_POSIX_FADVISE
  return NBDKIT_FUA_NATIVE;
#else
  return NBDKIT_FUA_EMULATE;
#endif
}

/* Flush the file to disk. */
static int
file_flush (void *handle, uint32_t flags)
{
  struct handle *h = handle;

  if (fdatasync (h->fd) == -1) {
    nbdkit_error ("fdatasync: %s: %m", h->name);
    return -1;
  }

  return 0;
}

/* Read data from the file. */
static int
file_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  struct handle *h = handle;
#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  const uint32_t orig_count = count;
  const uint64_t orig_offset = offset;
#endif

  while (count > 0) {
    ssize_t r = pread (h->fd, buf, count, offset);
    if (r == -1) {
      nbdkit_error ("pread: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                    h->name, offset, count);
      return -1;
    }
    if (r == 0) {
      nbdkit_error ("pread: %s: unexpected end of file", h->name);
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

#if defined (HAVE_POSIX_FADVISE) && defined (POSIX_FADV_DONTNEED)
  /* On Linux this will evict the pages we just read from the page cache. */
  if (reduce_memory_pressure)
    posix_fadvise (h->fd, orig_offset, orig_count, POSIX_FADV_DONTNEED);
#endif

  return 0;
}

/* Write data to the file. */
static int
file_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  struct handle *h = handle;
#if EVICT_WRITES
  const uint32_t orig_count = count;
  const uint64_t orig_offset = offset;
#endif

  while (count > 0) {
    ssize_t r = pwrite (h->fd, buf, count, offset);
    if (r == -1) {
      nbdkit_error ("pwrite: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                    h->name, offset, count);
      return -1;
    }
    buf += r;
    count -= r;
    offset += r;
  }

  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;

#if EVICT_WRITES
  if (reduce_memory_pressure) {
    if (evict_writes (h->fd, orig_offset, orig_count) == -1)
      return -1;
  }
#endif

  return 0;
}

#if defined(FALLOC_FL_PUNCH_HOLE) || defined(FALLOC_FL_ZERO_RANGE)
static int
do_fallocate (int fd, int mode_, off_t offset, off_t len)
{
  int r;

  r = fallocate (fd, mode_, offset, len);

  if (file_debug_zero)
    nbdkit_debug ("fallocate (["
#if defined(FALLOC_FL_PUNCH_HOLE)
                  "%s"
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
                  "%s"
#endif
                  " ], %" PRIu64 ", %" PRIu64") => %d (%d)",
#if defined(FALLOC_FL_PUNCH_HOLE)
                  mode_ & FALLOC_FL_PUNCH_HOLE ? " FALLOC_FL_PUNCH_HOLE" : "",
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
                  mode_ & FALLOC_FL_ZERO_RANGE ? " FALLOC_FL_ZERO_RANGE" : "",
#endif
                  (uint64_t) offset, (uint64_t) len, r,
                  r == -1 ? errno : 0);

  if (r == -1 && errno == ENODEV) {
    /* kernel 3.10 fails with ENODEV for block device. Kernel >= 4.9 fails
     * with EOPNOTSUPP in this case. Normalize errno to simplify callers.
     */
    errno = EOPNOTSUPP;
  }
  return r;
}
#endif

/* Write zeroes to the file. */
static int
file_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h __attribute__ ((unused)) = handle;
  const bool may_trim __attribute__ ((unused)) = flags & NBDKIT_FLAG_MAY_TRIM;

  /* These alternate zeroing methods are ordered.  Methods which can
   * trim (if may_trim is set) are tried first.  Methods which can
   * only zero are tried last.
   */
#ifdef FALLOC_FL_PUNCH_HOLE
  if (may_trim && h->can_punch_hole) {
    int r;

    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_punch_hole && may_trim: "
                      "zero succeeded using fallocate");
      goto out;
    }

    if (!is_enotsup (errno)) {
      nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                    h->name, offset, count);
      return -1;
    }

    ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_punch_hole"));
    h->can_punch_hole = false;
  }
#endif

#if defined(BLKDISCARD) && defined(FALLOC_FL_ZERO_RANGE)
  /* For aligned range and block device, we can use BLKDISCARD to
   * trim.  However BLKDISCARD doesn't necessarily zero (eg for local
   * disk) so we have to zero first and then discard.
   *
   * In future all Linux block devices may understand
   * FALLOC_FL_PUNCH_HOLE which means this case would no longer be
   * necessary, since the case above will handle it.
   */
  if (may_trim && h->can_blkdiscard && h->can_zero_range &&
      IS_ALIGNED (offset | count, h->sector_size)) {
    int r;
    uint64_t range[2] = {offset, count};

    r = do_fallocate (h->fd, FALLOC_FL_ZERO_RANGE, offset, count);
    if (r == 0) {
      /* We could use FALLOC_FL_PUNCH_HOLE here instead, but currently
       * thin LVs do not support it (XXX 2025-04).
       */
      r = ioctl (h->fd, BLKDISCARD, &range);
      if (r == 0) {
        if (file_debug_zero)
          nbdkit_debug ("h->can_blkdiscard && may_trim && IS_ALIGNED: "
                        "zero succeeded using BLKDISCARD");
        goto out;
      }

      if (!is_enotsup (errno)) {
        nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_blkdiscard"));
      h->can_blkdiscard = false;
    }
    else {
      if (!is_enotsup (errno)) {
        nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_fallocate"));
      h->can_fallocate = false;
    }
  }
#endif

#ifdef FALLOC_FL_ZERO_RANGE
  if (h->can_zero_range) {
    int r;

    r = do_fallocate (h->fd, FALLOC_FL_ZERO_RANGE, offset, count);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_zero_range: "
                      "zero succeeded using fallocate");
      goto out;
    }

    if (!is_enotsup (errno)) {
      nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                    h->name, offset, count);
      return -1;
    }

    ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_zero_range"));
    h->can_zero_range = false;
  }
#endif

#ifdef FALLOC_FL_PUNCH_HOLE
  /* If we can punch hole but may not trim, we can combine punching
   * hole and fallocate to zero a range. This is expected to be more
   * efficient than writing zeroes manually.
   */
  if (h->can_punch_hole && h->can_fallocate) {
    int r;

    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) {
      r = do_fallocate (h->fd, 0, offset, count);
      if (r == 0) {
        if (file_debug_zero)
          nbdkit_debug ("h->can_punch_hole && h->can_fallocate: "
                        "zero succeeded using fallocate");
        goto out;
      }

      if (!is_enotsup (errno)) {
        nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_fallocate"));
      h->can_fallocate = false;
    } else {
      if (!is_enotsup (errno)) {
        nbdkit_error ("zero: %s:  offset=%" PRIu64 ", count=%" PRIu32 ":%m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_punch_hole"));
      h->can_punch_hole = false;
    }
  }
#endif

#ifdef BLKZEROOUT
  /* For aligned range and block device, we can use BLKZEROOUT. */
  if (h->can_blkzeroout && IS_ALIGNED (offset | count, h->sector_size)) {
    int r;
    uint64_t range[2] = {offset, count};

    r = ioctl (h->fd, BLKZEROOUT, &range);
    if (r == 0) {
      if (file_debug_zero)
        nbdkit_debug ("h->can_blkzeroout && IS_ALIGNED: "
                      "zero succeeded using BLKZEROOUT");
      goto out;
    }

    if (errno != ENOTTY) {
      nbdkit_error ("zero: %s: offset=%" PRIu64 ", count=%" PRIu32 ": %m",
                    h->name, offset, count);
      return -1;
    }

    ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_blkzeroout"));
    h->can_blkzeroout = false;
  }
#endif

  /* Trigger a fall back to writing */
  if (file_debug_zero)
    ONCE (nbdkit_debug ("%s: zero falling back to writing", h->name));
  errno = EOPNOTSUPP;
  return -1;

#ifdef __clang__
  __attribute__ ((unused))
#endif
    out:
  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;
  return 0;
}

/* Punch a hole in the file. */
static int
file_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h __attribute__ ((unused)) = handle;

#ifdef FALLOC_FL_PUNCH_HOLE
  if (h->can_punch_hole) {
    int r;

    r = do_fallocate (h->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      offset, count);
    if (r == 0) goto out;
    if (r == -1) {
      if (!is_enotsup (errno)) {
        nbdkit_error ("fallocate: %s: offset=%" PRIu64 ", count=%" PRIu32 ":"
                      " %m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_punch_hole"));
      h->can_punch_hole = false;
    }
  }
#endif

#ifdef BLKDISCARD
  /* In future all Linux block devices may understand
   * FALLOC_FL_PUNCH_HOLE which means this case would no longer be
   * necessary, since the case above will handle it.
   */
  if (h->can_blkdiscard && IS_ALIGNED (offset | count, h->sector_size)) {
    int r;
    uint64_t range[2] = {offset, count};

    r = ioctl (h->fd, BLKDISCARD, &range);
    if (r == 0) goto out;
    if (r == -1) {
      if (!is_enotsup (errno)) {
        nbdkit_error ("ioctl: %s: offset=%" PRIu64 ", count=%" PRIu32 ":"
                      " BLKDISCARD: %m",
                      h->name, offset, count);
        return -1;
      }

      ONCE (nbdkit_debug ("%s: setting %s=false", h->name, "can_blkdiscard"));
      h->can_blkdiscard = false;
    }
  }
#endif

  /* Trim is advisory.  If we got here, we were unable to trim. */
  ONCE (nbdkit_debug ("%s: could not trim, no trim methods worked",
                      h->name));
  return 0;

#ifdef __clang__
  __attribute__ ((unused))
#endif
 out:
  if ((flags & NBDKIT_FLAG_FUA) && file_flush (handle, 0) == -1)
    return -1;

  return 0;
}

#ifdef SEEK_HOLE
/* Extents. */

static int
file_can_extents (void *handle)
{
  struct handle *h = handle;
  off_t r;

  /* A simple test to see whether SEEK_HOLE etc is likely to work on
   * the current filesystem.
   */
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
  r = lseek (h->fd, 0, SEEK_HOLE);
  if (r == -1) {
    nbdkit_debug ("lseek: SEEK_HOLE: error (ignored): %m");
    nbdkit_debug ("extents disabled because the kernel does not support "
                  "lseek and SEEK_HOLE for this file type");
    return 0;
  }
  return 1;
}

static int
do_extents (void *handle, uint32_t count, uint64_t offset,
            uint32_t flags, struct nbdkit_extents *extents)
{
  struct handle *h = handle;
  const bool req_one = flags & NBDKIT_FLAG_REQ_ONE;
  const uint64_t end = offset + count;

  do {
    off_t pos;

    pos = lseek (h->fd, offset, SEEK_DATA);
    if (pos == -1) {
      if (errno == ENXIO) {
        /* The current man page does not describe this situation well,
         * but a proposed change to POSIX adds these words for ENXIO:
         * "or the whence argument is SEEK_DATA and the offset falls
         * within the final hole of the file."
         */
        pos = end;
      }
      else {
        nbdkit_error ("lseek: SEEK_DATA: %" PRIu64 ": %m", offset);
        return -1;
      }
    }

    /* We know there is a hole from offset to pos-1. */
    if (pos > offset) {
      if (nbdkit_add_extent (extents, offset, pos - offset,
                             NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO) == -1)
        return -1;
      if (req_one)
        break;
    }

    offset = pos;
    if (offset >= end)
      break;

    pos = lseek (h->fd, offset, SEEK_HOLE);
    if (pos == -1) {
      nbdkit_error ("lseek: SEEK_HOLE: %" PRIu64 ": %m", offset);
      return -1;
    }

    /* We know there is data from offset to pos-1. */
    if (pos > offset) {
      if (nbdkit_add_extent (extents, offset, pos - offset,
                             0 /* allocated data */) == -1)
        return -1;
      if (req_one)
        break;
    }

    offset = pos;
  } while (offset < end);

  return 0;
}

static int
file_extents (void *handle, uint32_t count, uint64_t offset,
              uint32_t flags, struct nbdkit_extents *extents)
{
  ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&lseek_lock);
  return do_extents (handle, count, offset, flags, extents);
}
#endif /* SEEK_HOLE */

#if HAVE_POSIX_FADVISE
/* Caching. */
static int
file_cache (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  struct handle *h = handle;
  int r;

  /* This function returns an errno. */
  r = posix_fadvise (h->fd, offset, count, POSIX_FADV_WILLNEED);
  if (r) {
    errno = r;
    nbdkit_error ("posix_fadvise: %s: offset=%" PRIu64 ", count=%" PRIu32 ":"
                  " %m",
                  h->name, offset, count);
    return -1;
  }
  return 0;
}
#endif /* HAVE_POSIX_FADVISE */

static struct nbdkit_plugin plugin = {
  .name              = "file",
  .longname          = "nbdkit file plugin",
  .version           = PACKAGE_VERSION,
  .unload            = file_unload,
  .config            = file_config,
  .config_complete   = file_config_complete,
  .config_help       = file_config_help,
  .magic_config_key  = "file",
  .dump_plugin       = file_dump_plugin,
  .list_exports      = file_list_exports,
  .open              = file_open,
  .close             = file_close,
  .get_size          = file_get_size,
  .is_rotational     = file_is_rotational,
  .block_size        = file_block_size,
  .can_write         = file_can_write,
  .can_multi_conn    = file_can_multi_conn,
  .can_trim          = file_can_trim,
  .can_fua           = file_can_fua,
  .can_cache         = file_can_cache,
  .pread             = file_pread,
  .pwrite            = file_pwrite,
  .flush             = file_flush,
  .trim              = file_trim,
  .zero              = file_zero,
#ifdef SEEK_HOLE
  .can_extents       = file_can_extents,
  .extents           = file_extents,
#endif
#if HAVE_POSIX_FADVISE
  .cache             = file_cache,
#endif
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
