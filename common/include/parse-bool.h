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

#ifndef NBDKIT_PARSE_BOOL_H
#define NBDKIT_PARSE_BOOL_H

#include <string.h>
#include "ascii-string.h"

/* Parse a boolean-like string.  Returns:
 *  1 => string is true
 *  0 => string is false
 * -1 => string could not be parsed as a boolean
 */
static inline int
parse_bool (const char *str)
{
  if (!strcmp (str, "1") ||
      !ascii_strcasecmp (str, "true") ||
      !ascii_strcasecmp (str, "t") ||
      !ascii_strcasecmp (str, "yes") ||
      !ascii_strcasecmp (str, "y") ||
      !ascii_strcasecmp (str, "on"))
    return 1;

  if (!strcmp (str, "0") ||
      !ascii_strcasecmp (str, "false") ||
      !ascii_strcasecmp (str, "f") ||
      !ascii_strcasecmp (str, "no") ||
      !ascii_strcasecmp (str, "n") ||
      !ascii_strcasecmp (str, "off"))
    return 0;

  return -1;
}

#endif /* NBDKIT_PARSE_BOOL_H */
