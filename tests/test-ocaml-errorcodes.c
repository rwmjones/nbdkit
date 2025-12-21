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
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <libnbd.h>

#include "array-size.h"

#include "requires.h"

/* This test checks the conversion from OCaml exceptions,
 * including:
 *
 * - the Unix.error is converted to errno (in the plugin) to
 *   NBD_E* (over the wire) and back to errno (in libnbd).
 *
 * - the error string is as expected.
 *
 * Reading at various sector offsets in the associated plugin
 * (test_ocaml_errorcodes_plugin.ml) produces a predictable errno and
 * error message.
 */
static struct {
  uint64_t offset;              /* The offset we read from. */
  int expected_errno;           /* The expected errno. */
  const char *expected_error;   /* The expected error string (if
                                 * structured replies are supported).
                                 */
} tests[] = {
  {  0,     0,          NULL                        },
  {  1*512, EPERM,      "EPERM"                     },
  {  2*512, EIO,        "EIO"                       },
  {  3*512, ENOMEM,     "ENOMEM"                    },
  {  4*512, ESHUTDOWN,  "ESHUTDOWN"                 },
  {  5*512, EINVAL,     "EINVAL"                    },
  {  6*512, 0,          NULL                        },
  {  7*512, ENOMEM,     "malloc: test"              },
  {  8*512, EIO,        "invalid argument: testing" },
  {  9*512, ENOMEM,     "memory error"              },
  { 10*512, EIO,        "input/output error"        },
};

int
main (int argc, char *argv[])
{
  struct nbd_handle *nbd;
  char buf[512];
  size_t i;
  int sr;

#ifdef __APPLE__
  skip_because ("loading the OCaml plugin fails on macOS");
#endif

  nbd = nbd_create ();
  if (nbd == NULL) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

  if (nbd_connect_command (nbd,
                           (char *[]) {
                             "nbdkit", "-s", "--exit-with-parent", "-v",
                             "./test-ocaml-errorcodes-plugin.so",
                             NULL }) == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }

#ifdef LIBNBD_HAVE_NBD_GET_STRUCTURED_REPLIES_NEGOTIATED
  sr = nbd_get_structured_replies_negotiated (nbd);
  if (sr == -1) {
    fprintf (stderr, "%s\n", nbd_get_error ());
    exit (EXIT_FAILURE);
  }
#else
  sr = 0;
#endif

  if (nbd_pread (nbd, buf, 512, 0, 0) == -1) {
    fprintf (stderr, "%s: FAIL: did not expect reading sector 0 to fail\n",
             argv[0]);
    exit (EXIT_FAILURE);
  }

  for (i = 0; i < ARRAY_SIZE(tests); ++i) {
    int r;

    r = nbd_pread (nbd, buf, 512, tests[i].offset, 0);
    if (tests[i].expected_errno != 0) {
      int actual_errno;
      const char *actual_error;

      if (r != -1) {
        fprintf (stderr,
                 "%s: FAIL: reading sector %" PRIu64 " should have failed\n",
                 argv[0], tests[i].offset / 512);
        exit (EXIT_FAILURE);
      }

      actual_errno = nbd_get_errno ();
      actual_error = nbd_get_error ();
      printf ("actual_error: %d (\"%s\")\n", actual_errno, actual_error);
      fflush (stdout);
      if (actual_errno != tests[i].expected_errno) {
        fprintf (stderr, "%s: FAIL: actual errno = %d expected errno = %d\n",
                 argv[0], actual_errno, tests[i].expected_errno);
        exit (EXIT_FAILURE);
      }

      /* Unfortunately nbdkit does not yet send error messages in
       * structured replies.  When we implement that, we can check the
       * string here.
       */
      if (0 && sr && strcmp (actual_error, tests[i].expected_error) != 0) {
        fprintf (stderr, "%s: FAIL: actual error = \"%s\" "
                 "expected error = \"%s\"\n",
                 argv[0], actual_error, tests[i].expected_error);
        exit (EXIT_FAILURE);
      }
    }
    else {                      /* tests[i].expected_errno == 0 */
      if (r != 0) {
        fprintf (stderr,
                 "%s: FAIL: reading sector %" PRIu64 " should have succeeded\n",
                 argv[0], tests[i].offset / 512);
        exit (EXIT_FAILURE);
      }
    }
  }

  nbd_close (nbd);
  exit (EXIT_SUCCESS);
}
