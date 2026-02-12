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

/* nbdkit_parse_* exported functions. */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include "ascii-ctype.h"
#include "human-size.h"
#include "parse-bool.h"

#include "internal.h"

/* Since nbdkit 1.46 we have declared various function parameters as
 * nonnull.  But for older plugins we still have runtime checks for
 * NULL.  GCC has a very unhelpful (I'd argue, incorrect) warning when
 * you do this.  Get rid of it.
 */
#if defined (__GNUC__) && NBDKIT_GCC_VERSION >= 120000 /* gcc >= 12.0 */
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif

/* Common code for parsing integers. */
#define PARSE_COMMON_TAIL                                               \
  if (errno != 0) {                                                     \
    nbdkit_error ("%s: could not parse number: \"%s\": %m",             \
                  what, str);                                           \
    return -1;                                                          \
  }                                                                     \
  if (end == str) {                                                     \
    nbdkit_error ("%s: empty string where we expected a number",        \
                  what);                                                \
    return -1;                                                          \
  }                                                                     \
  if (*end) {                                                           \
    nbdkit_error ("%s: could not parse number: \"%s\": trailing garbage", \
                  what, str);                                           \
    return -1;                                                          \
  }                                                                     \
                                                                        \
  if (rp)                                                               \
    *rp = r;                                                            \
  return 0

/* Functions for parsing signed integers. */
NBDKIT_DLL_PUBLIC int
nbdkit_parse_int (const char *what, const char *str, int *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
#if INT_MAX != LONG_MAX
  if (r < INT_MIN || r > INT_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int8_t (const char *what, const char *str, int8_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
  if (r < INT8_MIN || r > INT8_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int16_t (const char *what, const char *str, int16_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
  if (r < INT16_MIN || r > INT16_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int32_t (const char *what, const char *str, int32_t *rp)
{
  long r;
  char *end;

  errno = 0;
  r = strtol (str, &end, 0);
#if INT32_MAX != LONG_MAX
  if (r < INT32_MIN || r > INT32_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_int64_t (const char *what, const char *str, int64_t *rp)
{
  long long r;
  char *end;

  errno = 0;
  r = strtoll (str, &end, 0);
#if INT64_MAX != LONGLONG_MAX
  if (r < INT64_MIN || r > INT64_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

/* Functions for parsing unsigned integers. */

/* strtou* functions have surprising behaviour if the first character
 * (after whitespace) is '-', so reject this early.
 */
#define PARSE_ERROR_IF_NEGATIVE                                         \
  do {                                                                  \
    while (ascii_isspace (*str))                                        \
      str++;                                                            \
    if (*str == '-') {                                                  \
      nbdkit_error ("%s: negative numbers are not allowed", what);      \
      return -1;                                                        \
    }                                                                   \
  } while (0)

NBDKIT_DLL_PUBLIC int
nbdkit_parse_unsigned (const char *what, const char *str, unsigned *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
#if UINT_MAX != ULONG_MAX
  if (r > UINT_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint8_t (const char *what, const char *str, uint8_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
  if (r > UINT8_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint16_t (const char *what, const char *str, uint16_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
  if (r > UINT16_MAX)
    errno = ERANGE;
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint32_t (const char *what, const char *str, uint32_t *rp)
{
  unsigned long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoul (str, &end, 0);
#if UINT32_MAX != ULONG_MAX
  if (r > UINT32_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_uint64_t (const char *what, const char *str, uint64_t *rp)
{
  unsigned long long r;
  char *end;

  PARSE_ERROR_IF_NEGATIVE;
  errno = 0;
  r = strtoull (str, &end, 0);
#if UINT64_MAX != ULONGLONG_MAX
  if (r > UINT64_MAX)
    errno = ERANGE;
#endif
  PARSE_COMMON_TAIL;
}

/* Parse a string as a size with possible scaling suffix, or return -1
 * after reporting the error.
 */
NBDKIT_DLL_PUBLIC int64_t
nbdkit_parse_size (const char *str)
{
  const char *error = NULL, *pstr = NULL;
  int64_t size;

  size = human_size_parse (str, &error, &pstr);
  if (size == -1) {
    nbdkit_error ("%s: %s", error, pstr);
    return -1;
  }

  return size;
}

NBDKIT_DLL_PUBLIC int
nbdkit_parse_probability (const char *what, const char *str,
                          double *retp)
{
  double d, d2;
  char c;
  int n;

  if (sscanf (str, "%lg%[:/]%lg%n", &d, &c, &d2, &n) == 3 &&
      strcmp (&str[n], "") == 0) { /* N:M or N/M */
    if (d == 0.0 && d2 == 0.0)     /* 0/0 is OK */
      ;
    else if (d2 == 0)              /* N/0 is bad */
      goto bad_parse;
    else
      d /= d2;
  }
  else if (sscanf (str, "%lg%n", &d, &n) == 1) {
    if (strcmp (&str[n], "%") == 0) /* percentage */
      d /= 100.0;
    else if (strcmp (&str[n], "") == 0) /* probability */
      ;
    else
      goto bad_parse;
  }
  else
    goto bad_parse;

  if (!isfinite (d))            /* reject NaN or inf */
    goto bad_parse;
  if (signbit (d))              /* reject negative numbers */
    goto bad_parse;

  if (retp)
    *retp = d;
  return 0;

 bad_parse:
  nbdkit_error ("%s: could not parse '%s' as a probability", what, str);
  return -1;
}

/* Parse a string as a boolean, or return -1 after reporting the error.
 */
NBDKIT_DLL_PUBLIC int
nbdkit_parse_bool (const char *str)
{
  int r = parse_bool (str);
  if (r == -1) {
    nbdkit_error ("could not decipher boolean (%s)", str);
    return -1;
  }
  else
    return r;
}

/* Parse a delay or sleep. */
NBDKIT_DLL_PUBLIC int
nbdkit_parse_delay (const char *what, const char *str,
                    unsigned *rsec, unsigned *rnsec)
{
  double d;
  int n;

  if (sscanf (str, "%lg%n", &d, &n) == 1) {
    if (strcmp (&str[n], "s") == 0 || strcmp (&str[n], "") == 0) {
      /* Seconds. */
    }
    else if (strcmp (&str[n], "ms") == 0) {
      /* Milliseconds. */
      d /= 1000;
    }
    else if (strcmp (&str[n], "us") == 0 || strcmp (&str[n], "μs") == 0) {
      /* Microseconds. */
      d /= 1000000;
    }
    else if (strcmp (&str[n], "ns") == 0) {
      /* Nanoseconds. */
      d /= 1000000000;
    }
    else
      goto bad_parse;
  }
  else {
  bad_parse:
    nbdkit_error ("%s: could not parse delay or sleep: \"%s\"",
                  what, str);
    return -1;
  }

  if (!isfinite (d))            /* reject NaN or inf */
    goto bad_parse;
  if (signbit (d))              /* reject negative numbers */
    goto bad_parse;

  if (rsec)
    *rsec = d;
  if (rnsec) {
    long m = lrint ((d - *rsec) * 1000000000.);
    *rnsec = m;
  }

  return 0;
}
