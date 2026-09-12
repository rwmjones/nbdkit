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

/* Miscellaneous public utility APIs to be exported by nbdkit for use
 * by filters and plugins, declared in nbdkit-common.h.
 *
 * For a list of all exported functions, see nbdkit.syms
 *
 * For other files containing exported functions see:
 *   - debug.c
 *   - exports.c
 *   - extents.c
 *   - parsing.c
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/time.h>

#ifdef WIN32
/* For nanosleep on Windows. */
#include <pthread_time.h>
#endif

#include "array-size.h"
#include "get_current_dir_name.h"
#include "poll.h"
#include "realpath.h"
#include "strndup.h"

#include "internal.h"

/* Since nbdkit 1.46 we have declared various function parameters as
 * nonnull.  But for older plugins we still have runtime checks for
 * NULL.  GCC has a very unhelpful (I'd argue, incorrect) warning when
 * you do this.  Get rid of it.
 */
#if defined (__GNUC__) && NBDKIT_GCC_VERSION >= 120000 /* gcc >= 12.0 */
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif

#ifndef WIN32

NBDKIT_DLL_PUBLIC char *
nbdkit_absolute_path (const char *path)
{
  CLEANUP_FREE char *pwd = NULL;
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot convert null or empty path to an absolute path");
    return NULL;
  }

  if (*path == '/') {
    ret = strdup (path);
    if (!ret) {
      nbdkit_error ("strdup: %m");
      return NULL;
    }
    return ret;
  }

  pwd = get_current_dir_name ();
  if (pwd == NULL) {
    nbdkit_error ("get_current_dir_name: %m");
    return NULL;
  }

  if (asprintf (&ret, "%s" DIR_SEPARATOR_STR "%s", pwd, path) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return ret;
}

#else /* WIN32 */

/* On Windows realpath() is replaced by GetFullPathName which doesn't
 * bother to check if the final path exists.  Therefore we can simply
 * replace nbdkit_absolute_path with nbdkit_realpath and everything
 * should work the same.
 */
NBDKIT_DLL_PUBLIC char *
nbdkit_absolute_path (const char *path)
{
  return nbdkit_realpath (path);
}

#endif /* WIN32 */

NBDKIT_DLL_PUBLIC char *
nbdkit_realpath (const char *path)
{
  char *ret;

  if (path == NULL || *path == '\0') {
    nbdkit_error ("cannot resolve a null or empty path");
    return NULL;
  }

  ret = realpath (path, NULL);
  if (ret == NULL) {
    nbdkit_error ("realpath: %s: %m", path);
    return NULL;
  }

  return ret;
}

/* Return true if it is safe to read from stdin during configuration. */
NBDKIT_DLL_PUBLIC int
nbdkit_stdio_safe (void)
{
  return !listen_stdin && !configured;
}

NBDKIT_DLL_PUBLIC int
nbdkit_nanosleep (unsigned sec, unsigned nsec)
{
  struct timespec ts;

  if (sec >= INT_MAX - nsec / 1000000000) {
    nbdkit_error ("sleep request is too long");
    errno = EINVAL;
    return -1;
  }
  ts.tv_sec = sec + nsec / 1000000000;
  ts.tv_nsec = nsec % 1000000000;

#if defined HAVE_PPOLL && defined POLLRDHUP
  /* End the sleep early if any of these happen:
   * - nbdkit has received a signal to shut down the server
   * - the current connection is multi-threaded and another thread detects
   *   NBD_CMD_DISC or a problem with the connection
   * - the input socket detects POLLRDHUP/POLLHUP/POLLERR
   * - the input socket is invalid (POLLNVAL, probably closed by
   *   another thread)
   */
  struct connection *conn = threadlocal_get_conn ();
  struct pollfd fds[] = {
    [0].fd = quit_fd,
    [0].events = POLLIN,
    [1].fd = conn ? conn->status_pipe[0] : -1,
    [1].events = POLLIN,
    [2].fd = conn ? conn->sockin : -1,
    [2].events = POLLRDHUP,
  };
  sigset_t all;

  /* Block all signals to this thread during the poll, so we don't
   * have to worry about EINTR
   */
  if (sigfillset (&all))
    abort ();
  switch (ppoll (fds, ARRAY_SIZE (fds), &ts, &all)) {
  case -1:
    assert (errno != EINTR);
    nbdkit_error ("poll: %m");
    return -1;
  case 0:
    return 0;
  }

  /* We don't have to read the pipe-to-self; if poll returned an
   * event, we know the connection should be shutting down.
   */
  bool has_quit = quit;
  assert (has_quit ||
          (conn && conn->nworkers > 0 &&
           connection_get_status () < STATUS_SHUTDOWN) ||
          (conn && (fds[2].revents & (POLLRDHUP | POLLHUP | POLLERR |
                                      POLLNVAL))));
  if (has_quit)
    nbdkit_error ("aborting sleep because of server shut down");
  else
    nbdkit_error ("aborting sleep because of connection close or error");
  errno = ESHUTDOWN;
  return -1;

#else
  /* The fallback path simply calls ordinary nanosleep, and will
   * cause long delays on server shutdown.
   *
   * If however you want to port this to your platform, then
   * porting ideas, in order of preference:
   * - POSIX requires pselect; it's a bit clunkier to set up than poll,
   *   but the same ability to atomically mask all signals and operate
   *   on struct timespec makes it similar to the preferred ppoll interface
   * - calculate an end time target, then use poll in a loop on EINTR with
   *   a recalculation of the timeout to still reach the end time (masking
   *   signals in that case is not safe, as it is a non-atomic race)
   */
  int r;

  r = nanosleep (&ts, NULL);
  if (r == -1 && errno != EINTR && errno != EAGAIN) {
    nbdkit_error ("nanosleep: %m");
    return -1;
  }
  return 0;
#endif
}

/* This function will be deprecated for API V3 users.  The preferred
 * approach will be to get the exportname from .open().
 *
 * Note: filters must not call this.  Use the exportname from filter
 * .open() method.
 */
NBDKIT_DLL_PUBLIC const char *
nbdkit_export_name (void)
{
  struct context *c = threadlocal_get_context ();

  if (!c) {
    nbdkit_error ("no context in this thread");
    return NULL;
  }
  if (!c->exportname) {
    nbdkit_error ("no context->exportname in this thread");
    return NULL;
  }

  return c->exportname;
}

/* This function will be deprecated for API V3 users.  The preferred
 * approach will be to get the tls mode from .open().
 */
NBDKIT_DLL_PUBLIC int
nbdkit_is_tls (void)
{
  struct context *c = threadlocal_get_context ();

  if (!c) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  if (!c->conn) {
    /* If a filter opened this backend outside of a client connection,
     * then we can only claim tls when the command line required it.
     */
    return tls == 2;
  }

  return c->conn->using_tls;
}

/* Functions for manipulating intern'd strings. */

static string_vector global_interns;

void
free_interns (void)
{
  struct connection *conn = threadlocal_get_conn ();
  string_vector *list = conn ? &conn->interns : &global_interns;

  string_vector_empty (list);
}

static const char *
add_intern (char *str)
{
  struct context *c = threadlocal_get_context ();
  struct connection *conn = c ? c->conn : NULL;
  string_vector *list = conn ? &conn->interns : &global_interns;

  if (string_vector_append (list, str) == -1) {
    nbdkit_error ("malloc: %m");
    free (str);
    return NULL;
  }

  return str;
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_strndup_intern (const char *str, size_t n)
{
  char *copy;

  if (str == NULL) {
    nbdkit_error ("nbdkit_strndup_intern: no string given");
    errno = EINVAL;
    return NULL;
  }

  copy = strndup (str, n);
  if (copy == NULL) {
    nbdkit_error ("strndup: %m");
    return NULL;
  }

  return add_intern (copy);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_strdup_intern (const char *str)
{
  char *copy;

  if (str == NULL) {
    nbdkit_error ("nbdkit_strdup_intern: no string given");
    errno = EINVAL;
    return NULL;
  }

  copy = strdup (str);
  if (copy == NULL) {
    nbdkit_error ("strdup: %m");
    return NULL;
  }

  return add_intern (copy);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_vprintf_intern (const char *fmt, va_list ap)
{
  char *str = NULL;

  if (vasprintf (&str, fmt, ap) == -1) {
    nbdkit_error ("asprintf: %m");
    return NULL;
  }

  return add_intern (str);
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_printf_intern (const char *fmt, ...)
{
  va_list ap;
  const char *ret;

  va_start (ap, fmt);
  ret = nbdkit_vprintf_intern (fmt, ap);
  va_end (ap);
  return ret;
}

NBDKIT_DLL_PUBLIC void
nbdkit_disconnect (int force)
{
  struct connection *conn = threadlocal_get_conn ();

  if (!conn) {
    debug ("no connection in this thread, ignoring disconnect request");
    return;
  }
  if (connection_set_status (force ? STATUS_DEAD : STATUS_SHUTDOWN)) {
    ACQUIRE_LOCK_FOR_CURRENT_SCOPE (&conn->write_lock);
    conn->close (SHUT_WR);
  }
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_name (void)
{
  return process_name;
}

static void
store_timestamp (char *timestamp, const size_t len)
{
#ifndef WIN32
  struct timeval tv = { 0 };
  struct tm tm;
  size_t n;

  gettimeofday (&tv, NULL);
  gmtime_r (&tv.tv_sec, &tm);

  n = strftime (timestamp, len, "%F %T", &tm);
  if (n == 0)
    /* Backup in case strftime fails, just store the seconds field. */
    snprintf (timestamp, len, "%lu", (unsigned long) tv.tv_sec);
  snprintf (timestamp + n, len - n, ".%06" PRIu64, (uint64_t) tv.tv_usec);
#else /* WIN32 */
  /* Windows doesn't have thread-safe gmtime, or the strftime %F and
   * %T formatters.  Let's try to do this in a Windows native way.
   * Windows SYSTEMTIME only returns milliseconds.
   */
  SYSTEMTIME st;
  GetSystemTime (&st);
  snprintf (timestamp, len, "%04d-%02d-%02d %02d:%02d:%02d.%03d000",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds);
#endif /* WIN32 */
}

NBDKIT_DLL_PUBLIC const char *
nbdkit_timestamp (void)
{
  char *timestamp = NULL;
  const size_t len = 64;

  timestamp = malloc (len);
  if (timestamp == NULL)
    goto err;

  store_timestamp (timestamp, len);

  /* Keep the timestamp string in thread-local storage.  This passes
   * ownership to threadlocal which will free it either on the next
   * call or if the thread exits.
   */
  if (threadlocal_set_timestamp (timestamp) == -1)
    goto err;

  return timestamp;

 err:
  free (timestamp);

  /* If allocating the timestamp in TLS fails, we need to return
   * something, so use a static buffer instead.  This is not thread
   * safe but it's the best we can do here.
   */
  static char static_timestamp[64];
  store_timestamp (static_timestamp, sizeof static_timestamp);
  return static_timestamp;
}
