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

/* Check for a requirement or skip the test. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "requires.h"

void
skip_because (const char *fs, ...)
{
  va_list args;

  printf ("Test skipped because: ");
  va_start (args, fs);
  vprintf (fs, args);
  va_end (args);
  putchar ('\n');
  fflush (stdout);
  exit (77);
}

void
requires (const char *cmd)
{
  printf ("requires %s\n", cmd);
  fflush (stdout);
  if (system (cmd) != 0)
    skip_because ("prerequisite is missing or not working");
}

void
requires_not (const char *cmd)
{
  printf ("requires_not %s\n", cmd);
  fflush (stdout);
  if (system (cmd) == 0)
    skip_because ("prerequisite is missing or not working");
}

void
requires_exists (const char *filename)
{
  printf ("requires_exists %s\n", filename);
  fflush (stdout);
  if (access (filename, F_OK) == -1)
    skip_because ("file '%s' not found", filename);
}

void
requires_not_exists (const char *filename)
{
  printf ("requires_not_exists %s\n", filename);
  fflush (stdout);
  if (access (filename, F_OK) == 0)
    skip_because ("file '%s' exists", filename);
}

void
requires_not_valgrind (const char *reason)
{
  const char *s = getenv ("NBDKIT_VALGRIND");
  if (s && strcmp (s, "1") == 0)
    skip_because ("%s", reason ? reason : "running under valgrind");
}

#ifndef WIN32
void
requires_root (void)
{
  if (geteuid () != 0)
    skip_because ("not running as root.\n"
                  "Use ‘sudo make check-root’ to run these tests.");
}
#else
void
requires_root (void)
{
  skip_because ("Windows requires_root not implemented");
}
#endif
