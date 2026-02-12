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

#ifndef NBDKIT_HUMAN_SIZE_TEST_CASES_H
#define NBDKIT_HUMAN_SIZE_TEST_CASES_H

#include <stdint.h>

/* Just some common test cases shared (in nbdkit) between
 * common/include/test-human-size.c and server/test-public.c
 */
static struct tuple {
  const char *str;
  int64_t res;
  const char *tail;
} tuples[] = {
  /* Bogus strings */
  { "", -1 },
  { " ", -1 },
  { "+ 1", -1 },
  { "garbage", -1 },

  /* Extracting substring value from larger string */
  { "0junk", 0, "junk" }, /* no suffix j */
  { "0garbage", 0, "arbage" }, /* 0g recognized */
  { "1 1", 1, " 1" },
  { "1M 1", 1024 * 1024, " 1" },

  /* Strings leading to overflow */
  { "9223372036854775808", -1 }, /* INT_MAX + 1 */
  { "18446744073709551614", -1 }, /* UINT64_MAX - 1 */
  { "18446744073709551615", -1 }, /* UINT64_MAX */
  { "18446744073709551616", -1 }, /* UINT64_MAX + 1 */
  { "999999999999999999999999", -1 },
  { "8E", -1 },
  { "8192P", -1 },

  /* Strings representing negative values */
  { "-1", -1 },
  { "-2", -1 },
  { "-9223372036854775809", -1 }, /* INT64_MIN - 1 */
  { "-9223372036854775808", -1 }, /* INT64_MIN */
  { "-9223372036854775807", -1 }, /* INT64_MIN + 1 */
  { "-18446744073709551616", -1 }, /* -UINT64_MAX - 1 */
  { "-18446744073709551615", -1 }, /* -UINT64_MAX */
  { "-18446744073709551614", -1 }, /* -UINT64_MAX + 1 */

  /* Strings we may want to support in the future */
  { "M", -1 }, /* Nicer would be 1024 * 1024 with tail "" */
  { "1MB", 1024 * 1024, "B" }, /* Nicer would be 1000 * 1000 with tail "" */
  { "1MiB", 1024 * 1024, "iB" }, /* Nicer would be 1024 * 1024 with tail "" */
  { "1.5M", 1, ".5M" }, /* Nicer would be 512 * 3 * 1024 with tail "" */
  { "1.5MB", 1, ".5MB" }, /* Nicer would be 1500 * 1000 with tail "" */

  /* Valid strings */
  { " -0", 0, "" },
  { "0", 0, "" },
  { "+0", 0, "" },
  { "1", 1, "" },
  { "+1", 1, "" },
  { "1234567890", 1234567890, "" },
  { "+1234567890", 1234567890, "" },
  { "9223372036854775807", INT64_MAX, "" },
  { "1s", 512, "" },
  { "2S", 1024, "" },
  { "1b", 1, "" },
  { "1B", 1, "" },
  { "1k", 1024, "" },
  { "1K", 1024, "" },
  { "1m", 1024 * 1024, "" },
  { "1M", 1024 * 1024, "" },
  { "+1M", 1024 * 1024, "" },
  { "1g", 1024 * 1024 * 1024, "" },
  { "1G", 1024 * 1024 * 1024, "" },
  { "1t", 1024LL * 1024 * 1024 * 1024, "" },
  { "1T", 1024LL * 1024 * 1024 * 1024, "" },
  { "1p", 1024LL * 1024 * 1024 * 1024 * 1024, "" },
  { "1P", 1024LL * 1024 * 1024 * 1024 * 1024, "" },
  { "8191p", 1024LL * 1024 * 1024 * 1024 * 1024 * 8191, "" },
  { "1e", 1024LL * 1024 * 1024 * 1024 * 1024 * 1024, "" },
  { "1E", 1024LL * 1024 * 1024 * 1024 * 1024 * 1024, "" },

  /* Hex strings (nbdkit 1.48) */
  { "0x0", 0, "" },
  { "0x200", 512, "" },
  { "0x200M", 536870912, "" },
  { "0x1_E", 1024LL * 1024 * 1024 * 1024 * 1024 * 1024, "" },

  /* Octal strings (nbdkit 1.48) */
  { "0377", 255, "" },
  { "08", 0, "8" }, /* in < 1.48, this parsed as 8 */
};

#endif /* NBDKIT_HUMAN_SIZE_TEST_CASES_H */
