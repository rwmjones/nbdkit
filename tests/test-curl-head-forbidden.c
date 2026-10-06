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

/* Test curl plugin against a simulated webserver which responds with
 * 403 Forbidden to HEAD requests, but allows the GET method.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <libnbd.h>

#include "cleanup.h"
#include "web-server.h"

#include "requires.h"
#include "test.h"

static char buf[1024];

int
main (int argc, char *argv[])
{
  struct stat statbuf;
  const char *sockpath;
  struct nbd_handle *nbd;
  CLEANUP_FREE char *usp_param = NULL;
  int64_t size;

  requires_exists ("disk");
  if (stat ("disk", &statbuf) == -1) {
    perror ("disk");
    exit (EXIT_FAILURE);
  }

  sockpath = web_server ("disk", NULL, true);
  if (sockpath == NULL) {
    fprintf (stderr, "%s: could not start web server thread\n", program_name);
    exit (EXIT_FAILURE);
  }

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Start nbdkit. */
  if (asprintf (&usp_param, "unix-socket-path=%s", sockpath) == -1) {
    perror ("asprintf");
    exit (EXIT_FAILURE);
  }
  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent", "-v",
                             "curl",
                             "-D", "curl.verbose=1",
                             "http://localhost/disk",
                             usp_param, /* unix-socket-path=... */
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  /* Check the size is expected. */
  size = nbd_get_size (nbd);
  if (size == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }
  if (size != statbuf.st_size) {
    fprintf (stderr, "%s: incorrect export size, "
             "expected: %" PRIu64 " actual: %" PRIi64 "\n",
             program_name,
             (uint64_t) statbuf.st_size, size);
    exit (EXIT_FAILURE);
  }

  /* Make a request. */
  if (nbd_pread (nbd, buf, sizeof buf, 0, 0) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
