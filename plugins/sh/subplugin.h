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

/* Both nbdkit-sh-plugin and nbdkit-eval-plugin are implemented using
 * common code from plugin/sh/.  These "sub-plugins" are abstracted
 * through the 'struct subplugin' interface.  There is one global
 * instance of this struct (per plugin), called 'sub'.
 */

#ifndef NBDKIT_SUBPLUGIN_H
#define NBDKIT_SUBPLUGIN_H

#include "nbdkit-string.h"

/* Exit codes. */
typedef enum exit_code {
  OK = 0,
  ERROR = 1,           /* all script error codes are mapped to this */
  MISSING = 2,         /* method missing */
  RET_FALSE = 3,       /* script exited with code 3 meaning false */
  SHUTDOWN_OK = 4,     /* call nbdkit_shutdown(), then return OK */
  SHUTDOWN_ERR = 5,    /* call nbdkit_shutdown(), then return ERROR */
  DISC_FORCE = 6,      /* call nbdkit_disconnect(true); return is irrelevant */
  DISC_SOFT_OK = 7,    /* call nbdkit_disconnect(false), return OK */
  DISC_SOFT_ERR = 8,   /* call nbdkit_disconnect(false), return ERROR */
  /* Adjust methods.c:sh_dump_plugin when defining new codes */
  /* 9-15 are reserved since 1.34; handle like ERROR for now */
} exit_code;

struct subplugin {
  /* Common methods in plugin/sh/methods.c use get_script to
   * initialize argv[0] before calling the call* functions below.
   *
   * From sh_dump_plugin and sh_thread_model ONLY it is possible for
   * this function to return NULL.  From all other contexts it must
   * return a script name.
   */
  const char *(*get_script) (const char *method);

  /* How to call most methods which require only a list of args.
   * argv[0] is the script filename.  argv[1] is the method name.
   */
  exit_code (*call) (const char **argv)
    NBDKIT_ATTRIBUTE_NONNULL ((1));

  /* For methods which return a string.  Note that rbuf should be a
   * variable initialized to empty_vector.  On success it is allocated
   * by this function.
   */
  exit_code (*call_read) (string *rbuf, const char **argv)
    NBDKIT_ATTRIBUTE_NONNULL ((1, 2));

  /* For methods which take an input buffer.  Only pwrite uses this. */
  exit_code (*call_write) (const char *wbuf, size_t wbuflen,
                           const char **argv)
    NBDKIT_ATTRIBUTE_NONNULL ((1, 3));
};

extern struct subplugin sub;

#endif /* NBDKIT_SUBPLUGIN_H */
