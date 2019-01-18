/* nbdkit
 * Copyright (C) 2019 Red Hat Inc.
 * All rights reserved.
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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "internal.h"

/* True if we forked into the background (used to control log messages). */
bool forked_into_background;

#ifndef WIN32

/* Run as a background process.  If foreground is set (ie. -f or
 * equivalent) then this does nothing.  Otherwise it forks into the
 * background and sets forked_into_background.
 */
void
fork_into_background (void)
{
  pid_t pid;

  if (foreground)
    return;

  pid = fork ();
  if (pid == -1) {
    perror ("fork");
    exit (EXIT_FAILURE);
  }

  if (pid > 0)                  /* Parent process exits. */
    exit (EXIT_SUCCESS);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  chdir ("/");
#pragma GCC diagnostic pop

  /* Close stdin/stdout and redirect to /dev/null. */
  close (0);
  close (1);
  open ("/dev/null", O_RDONLY);
  open ("/dev/null", O_WRONLY);

  /* If not verbose, set stderr to the same as stdout as well. */
  if (!verbose)
    dup2 (1, 2);

  forked_into_background = true;
  debug ("forked into background (new pid = %d)", getpid ());
}

#else /* WIN32 */

void
fork_into_background (void)
{
  if (foreground)
    return;

  fprintf (stderr, "%s: cannot fork into background on Windows.\n"
           "You must use -f or equivalent option.\n",
           program_name);
  exit (EXIT_FAILURE);
}

#endif /* WIN32 */
