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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "internal.h"

socket_activation_list socket_activation;

#ifndef WIN32

#define FIRST_SOCKET_ACTIVATION_FD 3 /* defined by systemd ABI */

/* Handle socket activation.  This is controlled through special
 * environment variables inherited by nbdkit.  Sets up the list of
 * file descriptors in global 'socket_activation'.  See also
 * virGetListenFDs in libvirt.org:src/util/virutil.c
 */
void
get_socket_activation (void)
{
  const char *s;
  unsigned int pid;
  unsigned int nr_fds;
  unsigned int i;
  int fd;
  int f;
  char *name;
  size_t namelen;

  s = getenv ("LISTEN_PID");
  if (s == NULL)
    goto out;
  if (nbdkit_parse_unsigned ("LISTEN_PID", s, &pid) == -1)
    goto out;
  if (pid != getpid ()) {
    fprintf (stderr, "%s: %s was not for us (ignored)\n",
             program_name, "LISTEN_PID");
    goto out;
  }

  s = getenv ("LISTEN_FDS");
  if (s == NULL)
    goto out;
  if (nbdkit_parse_unsigned ("LISTEN_FDS", s, &nr_fds) == -1)
    goto out;

  /* Limit the number of fds that may be passed in to something
   * reasonable.
   */
  if (nr_fds == 0 || nr_fds > 16) {
    fprintf (stderr, "%s: socket activation: LISTEN_FDS=%s out of range\n",
             program_name, s);
    exit (EXIT_FAILURE);
  }

  /* Build a list of file descriptors and names.  Test each file
   * descriptor and mark it as FD_CLOEXEC so they don't leak into
   * child processes.
   */
  s = getenv ("LISTEN_FDNAMES");
  for (i = 0; i < nr_fds; ++i) {
    fd = FIRST_SOCKET_ACTIVATION_FD + i;
    f = fcntl (fd, F_GETFD);
    if (f == -1 || fcntl (fd, F_SETFD, f | FD_CLOEXEC) == -1) {
      /* If we cannot set FD_CLOEXEC then it probably means the file
       * descriptor is invalid, so socket activation has gone wrong
       * and we should exit.
       */
      fprintf (stderr, "%s: socket activation: "
               "invalid file descriptor fd = %d: %s\n",
               program_name, fd, strerror (errno));
      exit (EXIT_FAILURE);
    }

    /* LISTEN_FDNAMES is a colon-separated list of names (if defined).
     * Get the next name.
     */
    name = NULL;
    if (s && *s) {
      namelen = strcspn (s, ":");
      /* libnbd at least uses "unknown" to mean no name.  I'm not sure
       * if that is part of the socket activation "standard".
       */
      if (namelen > 0 && strncmp (s, "unknown", namelen) != 0) {
        name = strndup (s, namelen);
        if (name == NULL) {
          perror ("strndup");
          exit (EXIT_FAILURE);
        }
      }
      s += namelen;
      if (*s == ':') s++;
    }

    struct socket_activation_socket sact = { .fd = fd, .name = name };
    if (socket_activation_list_append (&socket_activation, sact) == -1) {
      perror ("socket_activation_list_append: realloc");
      exit (EXIT_FAILURE);
    }
  }

 out:
  /* So these are not passed to any child processes we might start. */
  unsetenv ("LISTEN_FDS");
  unsetenv ("LISTEN_FDNAMES");
  unsetenv ("LISTEN_PID");
}

#else /* WIN32 */

void
get_socket_activation (void)
{
  /* not supported */
}

#endif /* WIN32 */

void
free_socket_activation (void)
{
  size_t i;

  for (i = 0; i < socket_activation.len; ++i) {
    free (socket_activation.ptr[i].name);
    socket_activation.ptr[i].name = NULL;
  }
  socket_activation_list_reset (&socket_activation);
}
