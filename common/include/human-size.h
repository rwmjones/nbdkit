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

#ifndef NBDKIT_HUMAN_SIZE_H
#define NBDKIT_HUMAN_SIZE_H

#include <stdint.h>
#include <inttypes.h>
#include <errno.h>

/* Attempt to parse a string with a possible scaling suffix, such as
 * "2M".  Disk sizes cannot usefully exceed off_t (which is signed)
 * and cannot be negative.  If rest is not NULL, the number being
 * parsed is treated as a substring within a larger input; if a value
 * was parsed, *rest is set to the first unparsed byte of str.  If
 * rest is NULL, any trailing garbage is treated as an error.
 *
 * On error, returns -1 and sets *error and *pstr.  You can form a
 * final error message by appending "<error>: <pstr>".
 */
static inline int64_t __attribute__ ((__nonnull__ (1, 3, 4)))
human_size_parse_substr (const char *str, char **rest,
                         const char **error, const char **pstr)
{
  int64_t size;
  char *end;
  uint64_t scale = 1;

  /* XXX Should we also parse things like '1.5M'? */
  if (rest)
    *rest = NULL;
  *error = *pstr = NULL;
  errno = 0;
  size = strtoimax (str, &end, 0);
  if (str == end) {
    *error = "could not parse size string";
    *pstr = str;
    return -1;
  }
  if (size < 0) {
    *error = "size cannot be negative";
    *pstr = str;
    return -1;
  }
  if (errno) {
    *error = "size exceeds maximum value";
    *pstr = str;
    return -1;
  }

  /* Since we already filtered out empty string, it is safe to skip
   * the scale factor here if *end == '\0'
   */
  if (*end != '\0') {
    char *prev_end = end;

    /* Allow a single _ before the scaling factor so that hexadecimal
     * can be parsed above, eg. "0x1_E"
     */
    if (*end == '_')
      end++;

    switch (*end) {
      /* Powers of 1024 */
    case 'e': case 'E':
      scale *= 1024;
      /* fallthru */
    case 'p': case 'P':
      scale *= 1024;
      /* fallthru */
    case 't': case 'T':
      scale *= 1024;
      /* fallthru */
    case 'g': case 'G':
      scale *= 1024;
      /* fallthru */
    case 'm': case 'M':
      scale *= 1024;
      /* fallthru */
    case 'k': case 'K':
      scale *= 1024;
      /* fallthru */
    case 'b': case 'B':
      end++;
      break;

      /* "sectors", ie. units of 512 bytes, even if that's not the real
       * sector size
       */
    case 's': case 'S':
      scale = 512;
      end++;
      break;

      /* If we didn't manage to parse a scale, reset 'end'. */
    default:
      end = prev_end;
    }
  }

  if (INT64_MAX / scale < size) {
    *error = "could not parse size: size * scale overflows";
    *pstr = str;
    return -1;
  }

  /* XXX Maybe we should support 'MiB' as a synonym for 'M'; and 'MB'
   * for powers of 1000, for similarity to GNU tools. But for now,
   * anything beyond 'M' is dropped.
   */
  if (rest)
    *rest = end;
  else if (*end) {
    *error = "could not parse size: unknown suffix";
    *pstr = end;
    return -1;
  }

  return size * scale;
}

/* Attempt to parse a string with a possible scaling suffix, such as
 * "2M".  Disk sizes cannot usefully exceed off_t (which is signed)
 * and cannot be negative.  str must not have any trailing garbage.
 *
 * On error, returns -1 and sets *error and *pstr.  You can form a
 * final error message by appending "<error>: <pstr>".
 */
static inline int64_t __attribute__ ((__nonnull__ (1, 2, 3)))
human_size_parse (const char *str,
                  const char **error, const char **pstr)
{
  return human_size_parse_substr (str, NULL, error, pstr);
}


#endif /* NBDKIT_HUMAN_SIZE_H */
