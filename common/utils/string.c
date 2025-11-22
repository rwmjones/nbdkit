/* nbdkit
 * Copyright Red Hat
 *
 * This is based on code from util-linux/lib/blkdev.c which is
 * distributed under a compatible license.
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

#include "cleanup.h"
#include "nbdkit-string.h"

ssize_t
string_append_format (string *s, const char *fs, ...)
{
  CLEANUP_FREE char *s2 = NULL;
  va_list ap;
  size_t len;
  int r;

  va_start (ap, fs);
  r = vasprintf (&s2, fs, ap);
  va_end (ap);
  if (r == -1) return -1;

  /* Make sure the string is always \0-terminated by ensuring the
   * reservation is 1 byte longer than we need.
   */
  len = strlen (s2);
  if (string_reserve (s, len + 1) == -1)
    return -1;

  string_append_array (s, s2, len);

  /* Make sure the string is \0-terminated in the byte of space
   * reserved after the string.
   */
  s->ptr[s->len] = '\0';

  return s->len;
}
