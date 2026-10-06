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
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include <curl/curl.h>

#include <nbdkit-plugin.h>

#include "array-size.h"

#include "curldefs.h"

/* Use '-D curl.times=1' to set. */
NBDKIT_DLL_PUBLIC int curl_debug_times = 0;

/* The cumulative times. */
static struct {
  bool cumulative;
  const char *name;
  CURLINFO info;
  _Atomic curl_off_t t;
} times[] = {
  { true, "name resolution", CURLINFO_NAMELOOKUP_TIME_T },
  { true, "connection", CURLINFO_CONNECT_TIME_T },
  { true, "SSL negotiation", CURLINFO_APPCONNECT_TIME_T },
  { true, "pretransfer", CURLINFO_PRETRANSFER_TIME_T },
  { true, "first byte received", CURLINFO_STARTTRANSFER_TIME_T },
  { true, "data transfer", CURLINFO_TOTAL_TIME_T },
  { false, "redirection time", CURLINFO_REDIRECT_TIME_T },
};

/* This is called after every curl_easy_perform.  If -D curl.times=1
 * then we update the time counters.  Refer to curl_easy_getinfo(3)
 * section "TIMES".
 */
void
update_times (CURL *c)
{
  size_t i;
  CURLcode r;
  curl_off_t t;

  if (!curl_debug_times) return;

  for (i = 0; i < ARRAY_SIZE (times); ++i) {
    r = curl_easy_getinfo (c, times[i].info, &t);
    if (r != CURLE_OK) {
      nbdkit_debug ("curl_easy_getinfo: error getting time '%s': %s",
                    times[i].name, curl_easy_strerror (r));
      continue;
    }
    if (curl_debug_verbose)
      nbdkit_debug ("time '%s': %" PRIi64, times[i].name, (int64_t) t);
    times[i].t += t;
  }
}

/* Called when the plugin is unloaded. */
void
display_times (void)
{
  size_t i;
  int64_t prev_t = 0, t, v;

  if (!curl_debug_times) return;

  nbdkit_debug ("times (-D curl.times=1):");
  for (i = 0; i < ARRAY_SIZE (times); ++i) {
    t = times[i].t;             /* in microseconds */
    if (times[i].cumulative)
      v = t - prev_t;
    else
      v = t;
    prev_t = t;

    nbdkit_debug ("%-30s: %4" PRIi64 ".%06" PRIi64 " s",
                  times[i].name,
                  v / 1000000, v % 1000000);
  }
}
