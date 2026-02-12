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

/* Read password parameters. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include "getline.h"

#include "internal.h"

/* Since nbdkit 1.46 we have declared various function parameters as
 * nonnull.  But for older plugins we still have runtime checks for
 * NULL.  GCC has a very unhelpful (I'd argue, incorrect) warning when
 * you do this.  Get rid of it.
 */
#if defined (__GNUC__) && NBDKIT_GCC_VERSION >= 120000 /* gcc >= 12.0 */
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif

/* Read a password from configuration value. */
static int read_password_interactive (char **password);
static int read_password_from_fd (const char *what, int fd, char **password);

NBDKIT_DLL_PUBLIC int
nbdkit_read_password (const char *value, char **password)
{
  *password = NULL;

  /* Read from stdin interactively. */
  if (strcmp (value, "-") == 0) {
    if (read_password_interactive (password) == -1)
      return -1;
  }

  /* Read from numbered file descriptor. */
  else if (value[0] == '-') {
#ifndef WIN32
    int fd;

    if (nbdkit_parse_int ("password file descriptor", &value[1], &fd) == -1)
      return -1;
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
      nbdkit_error ("cannot use password -FD for stdin/stdout/stderr");
      return -1;
    }
    if (read_password_from_fd (&value[1], fd, password) == -1)
      return -1;

#else /* WIN32 */
    /* As far as I know this will never be possible on Windows, so
     * it's a simple error.
     */
    nbdkit_error ("not possible to read passwords from file descriptors "
                  "under Windows");
    return -1;
#endif /* WIN32 */
  }

  /* Read password from a file. */
  else if (value[0] == '+') {
    int fd;

    fd = open (&value[1], O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
      nbdkit_error ("open %s: %m", &value[1]);
      return -1;
    }
    if (read_password_from_fd (&value[1], fd, password) == -1)
      return -1;
  }

  /* Parameter is the password. */
  else {
    *password = strdup (value);
    if (*password == NULL) {
      nbdkit_error ("strdup: %m");
      return -1;
    }
  }

  return 0;
}

#ifndef WIN32

typedef struct termios echo_mode;

static void
echo_off (echo_mode *old_mode)
{
  struct termios temp;

  tcgetattr (STDIN_FILENO, old_mode);
  temp = *old_mode;
  temp.c_lflag &= ~ECHO;
  tcsetattr (STDIN_FILENO, TCSAFLUSH, &temp);
}

static void
echo_restore (const echo_mode *old_mode)
{
  tcsetattr (STDIN_FILENO, TCSAFLUSH, old_mode);
}

#else /* WIN32 */

/* Windows implementation of tty echo off based on this:
 * https://stackoverflow.com/a/1455007
 */
typedef DWORD echo_mode;

static void
echo_off (echo_mode *old_mode)
{
  HANDLE h_stdin;
  DWORD mode;

  h_stdin = GetStdHandle (STD_INPUT_HANDLE);
  GetConsoleMode (h_stdin, old_mode);
  mode = *old_mode;
  mode &= ~ENABLE_ECHO_INPUT;
  SetConsoleMode (h_stdin, mode);
}

static void
echo_restore (const echo_mode *old_mode)
{
  HANDLE h_stdin;

  h_stdin = GetStdHandle (STD_INPUT_HANDLE);
  SetConsoleMode (h_stdin, *old_mode);
}

#endif /* WIN32 */

static int
read_password_interactive (char **password)
{
  int err;
  echo_mode orig;
  ssize_t r;
  size_t n;

  if (!nbdkit_stdio_safe ()) {
    nbdkit_error ("stdin is not available for reading password");
    return -1;
  }

  if (!isatty (STDIN_FILENO)) {
    nbdkit_error ("stdin is not a tty, cannot read password interactively");
    return -1;
  }

  printf ("password: ");

  /* Set no echo. */
  echo_off (&orig);

  /* To distinguish between error and EOF we have to check errno.
   * getline can return -1 and errno = 0 which means we got end of
   * file, which is simply a zero length password.
   */
  errno = 0;
  r = getline (password, &n, stdin);
  err = errno;

  /* Restore echo. */
  echo_restore (&orig);

  /* Complete the printf above. */
  printf ("\n");

  if (r == -1) {
    if (err == 0) {             /* EOF, not an error. */
      free (*password);         /* State of linebuf is undefined. */
      *password = strdup ("");
      if (*password == NULL) {
        nbdkit_error ("strdup: %m");
        return -1;
      }
    }
    else {
      errno = err;
      nbdkit_error ("could not read password from stdin: %m");
      return -1;
    }
  }

  if (*password && r > 0 && (*password)[r-1] == '\n')
    (*password)[r-1] = '\0';

  return 0;
}

static int
read_password_from_fd (const char *what, int fd, char **password)
{
  FILE *fp;
  size_t n;
  ssize_t r;
  int err;

  fp = fdopen (fd, "r");
  if (fp == NULL) {
    nbdkit_error ("fdopen %s: %m", what);
    close (fd);
    return -1;
  }

  /* To distinguish between error and EOF we have to check errno.
   * getline can return -1 and errno = 0 which means we got end of
   * file, which is simply a zero length password.
   */
  errno = 0;
  r = getline (password, &n, fp);
  err = errno;

  fclose (fp);

  if (r == -1) {
    if (err == 0) {             /* EOF, not an error. */
      free (*password);         /* State of linebuf is undefined. */
      *password = strdup ("");
      if (*password == NULL) {
        nbdkit_error ("strdup: %m");
        return -1;
      }
    }
    else {
      errno = err;
      nbdkit_error ("could not read password from %s: %m", what);
      return -1;
    }
  }

  if (*password && r > 0 && (*password)[r-1] == '\n')
    (*password)[r-1] = '\0';

  return 0;
}
