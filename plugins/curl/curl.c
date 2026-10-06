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

/* How this plugin works
 * =====================
 *
 * Curl handle configuration from the nbdkit command line is all done
 * in config.c.  This file also contains a function to allocate fully
 * configured curl easy handles.
 *
 * The main nbdkit threads (this file) create curl easy handles
 * initialized with the work they want to carry out.  Note there is
 * one easy handle per request (eg. per pread/pwrite request).  The
 * easy handles are not reused.
 *
 * There is a background worker thread (worker.c) which has a single
 * curl multi handle.
 *
 * The commands (including the easy handle) are submitted to the
 * worker thread over a self-pipe.  It's easy to use a pipe for this
 * because the way curl multi works it can listen on an extra fd, but
 * not on anything else like a pthread condition.  In the worker
 * thread the curl multi performs the work of the outstanding easy
 * handles.
 *
 * When an easy handle finishes work or errors, we retire the command
 * by signalling back to the waiting nbdkit thread using a pthread
 * condition.
 *
 * In my experiments, we're almost always I/O bound so I haven't seen
 * any strong need to use more than one curl multi and/or worker
 * thread, although it would be possible to add more in future.
 *
 * See also this extremely useful thread:
 * https://curl.se/mail/lib-2019-03/0100.html
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <curl/curl.h>

#include <nbdkit-plugin.h>

#include "ascii-ctype.h"
#include "ascii-string.h"

#include "curldefs.h"

const char *cookie_script = NULL;
unsigned cookie_script_renew = 0;
const char *header_script = NULL;
unsigned header_script_renew = 0;

static void
curl_load (void)
{
  CURLcode r;

  r = curl_global_init (CURL_GLOBAL_DEFAULT);
  if (r != CURLE_OK) {
    nbdkit_error ("libcurl initialization failed: %d", (int) r);
    exit (EXIT_FAILURE);
  }
}

int
curl_get_ready (void)
{
  return worker_get_ready ();
}

int
curl_after_fork (void)
{
  return worker_after_fork ();
}

static void
curl_unload (void)
{
  worker_unload ();
  config_unload ();
  scripts_unload ();
  display_times ();
  curl_global_cleanup ();
}

/* Create the per-connection handle. */
static void *
curl_open (int readonly)
{
  struct handle *h;

  h = calloc (1, sizeof *h);
  if (h == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }
  h->readonly = readonly;

  return h;
}

/* Free up the per-connection handle. */
static void
curl_close (void *handle)
{
  struct handle *h = handle;

  free (h);
}

#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

/* Multi-conn is safe for read-only connections, but HTTP does not
 * have any concept of flushing so we cannot use it for read-write
 * connections.
 */
static int
curl_can_multi_conn (void *handle)
{
  struct handle *h = handle;

  return !! h->readonly;
}

/* Get the file size. */
static int get_content_length_accept_range (struct curl_handle *ch);
static bool try_fallback_GET_method (struct curl_handle *ch);
static size_t header_cb (void *ptr, size_t size, size_t nmemb, void *opaque);
static size_t error_cb (char *ptr, size_t size, size_t nmemb, void *opaque);

static int64_t
curl_get_size (void *handle)
{
  struct curl_handle *ch;
  CURLcode r;
  long code;
  curl_off_t o;
  int64_t exportsize;

  /* Get a curl easy handle. */
  ch = allocate_handle ();
  if (ch == NULL) goto err;

  /* Prepare to read the headers. */
  if (get_content_length_accept_range (ch) == -1)
    goto err;

  /* Send the command to the worker thread and wait. */
  struct command cmd = {
    .type = EASY_HANDLE,
    .ch = ch,
  };

  r = send_command_to_worker_and_wait (&cmd);
  update_times (ch->c);
  if (r != CURLE_OK) {
    display_curl_error (ch, r,
                        "problem doing HEAD request to fetch size of URL [%s]",
                        url);

    /* Get the HTTP status code, if available. */
    r = curl_easy_getinfo (ch->c, CURLINFO_RESPONSE_CODE, &code);
    if (r == CURLE_OK)
      nbdkit_debug ("HTTP status code: %ld", code);
    else
      code = -1;

    /* See comment on try_fallback_GET_method below. */
    if (code != 403 || !try_fallback_GET_method (ch))
      goto err;
  }

  /* Get the content length.
   *
   * Note there is some subtlety here: For web servers using chunked
   * encoding, either the Content-Length header will not be present,
   * or if present it should be ignored.  (For such servers the only
   * way to find out the true length would be to read all of the
   * content, which we don't want to do).
   *
   * Curl itself resolves this for us.  It will ignore the
   * Content-Length header if chunked encoding is used, returning the
   * length as -1 which we check below (see also
   * curl:lib/http.c:Curl_http_size).
   */
  r = curl_easy_getinfo (ch->c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &o);
  if (r != CURLE_OK) {
    display_curl_error (ch, r,
                        "could not get length of remote file [%s]", url);
    goto err;
  }

  if (o == -1) {
    nbdkit_error ("could not get length of remote file [%s], "
                  "is the URL correct?", url);
    goto err;
  }

  exportsize = o;
  nbdkit_debug ("content length: %" PRIi64, exportsize);

  /* If this is HTTP, check that byte ranges are supported. */
  if (ascii_strncasecmp (url, "http://", strlen ("http://")) == 0 ||
      ascii_strncasecmp (url, "https://", strlen ("https://")) == 0) {
    if (!ch->accept_range) {
      nbdkit_error ("server does not support 'range' (byte range) requests");
      goto err;
    }

    nbdkit_debug ("accept range supported (for HTTP/HTTPS)");
  }

  free_handle (ch);
  return exportsize;

 err:
  if (ch)
    free_handle (ch);
  return -1;
}

/* Get the file size and also whether the remote HTTP server
 * supports byte ranges.
 */
static int
get_content_length_accept_range (struct curl_handle *ch)
{
  /* We must run the scripts if necessary and set headers in the
   * handle.
   */
  if (do_scripts (ch) == -1)
    return -1;

  /* Set this flag in the handle to false.  The callback should set it
   * to true if byte ranges are supported, which we check below.
   */
  ch->accept_range = false;

  /* No Body, not nobody!  This forces a HEAD request. */
  curl_easy_setopt (ch->c, CURLOPT_NOBODY, 1L);
  curl_easy_setopt (ch->c, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt (ch->c, CURLOPT_HEADERDATA, ch);
  curl_easy_setopt (ch->c, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt (ch->c, CURLOPT_WRITEDATA, NULL);
  curl_easy_setopt (ch->c, CURLOPT_READFUNCTION, NULL);
  curl_easy_setopt (ch->c, CURLOPT_READDATA, NULL);
  return 0;
}

/* S3 servers can return 403 Forbidden for HEAD but still respond
 * to GET, so we give it a second chance in that case.
 * https://github.com/kubevirt/containerized-data-importer/issues/2737
 *
 * This function issues a GET request with a writefunction that always
 * returns an error, thus effectively getting the headers but
 * abandoning the transfer as soon as possible after.
 */
static bool
try_fallback_GET_method (struct curl_handle *ch)
{
  CURLcode r;

  nbdkit_debug ("attempting to fetch headers using GET method");

  curl_easy_setopt (ch->c, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt (ch->c, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt (ch->c, CURLOPT_HEADERDATA, ch);
  curl_easy_setopt (ch->c, CURLOPT_WRITEFUNCTION, error_cb);
  curl_easy_setopt (ch->c, CURLOPT_WRITEDATA, ch);

  struct command cmd = {
    .type = EASY_HANDLE,
    .ch = ch,
  };

  r = send_command_to_worker_and_wait (&cmd);
  update_times (ch->c);

  /* We expect CURLE_WRITE_ERROR here, but CURLE_OK is possible too
   * (eg if the remote has zero length).  Other errors might happen
   * but we ignore them since it is a fallback path.
   */
  return r == CURLE_OK || r == CURLE_WRITE_ERROR;
}

static size_t
header_cb (void *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *ch = opaque;
  size_t realsize = size * nmemb;
  const char *header = ptr;
  const char *end = header + realsize;
  const char *accept_ranges = "accept-ranges:";
  const char *bytes = "bytes";

  if (realsize >= strlen (accept_ranges) &&
      ascii_strncasecmp (header, accept_ranges, strlen (accept_ranges)) == 0) {
    const char *p = strchr (header, ':') + 1;

    /* Skip whitespace between the header name and value. */
    while (p < end && *p && ascii_isspace (*p))
      p++;

    if (end - p >= strlen (bytes)
        && strncmp (p, bytes, strlen (bytes)) == 0) {
      /* Check that there is nothing but whitespace after the value. */
      p += strlen (bytes);
      while (p < end && *p && ascii_isspace (*p))
        p++;

      if (p == end || !*p)
        ch->accept_range = true;
    }
  }

  return realsize;
}

static size_t
error_cb (char *ptr, size_t size, size_t nmemb, void *opaque)
{
#ifdef CURL_WRITEFUNC_ERROR
  return CURL_WRITEFUNC_ERROR;
#else
  return 0; /* in older curl, any size < requested will also be an error */
#endif
}

/* Read data from the remote server. */
static size_t write_cb (char *ptr, size_t size, size_t nmemb, void *opaque);

static int
curl_pread (void *handle, void *buf, uint32_t count, uint64_t offset)
{
  CURLcode r;
  struct curl_handle *ch;
  char range[128];

  /* Get a curl easy handle. */
  ch = allocate_handle ();
  if (ch == NULL) goto err;

  /* Run the scripts if necessary and set headers in the handle. */
  if (do_scripts (ch) == -1) goto err;

  /* Tell the write_cb where we want the data to be written.  write_cb
   * will update this if the data comes in multiple sections.
   */
  curl_easy_setopt (ch->c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt (ch->c, CURLOPT_WRITEDATA, ch);
  ch->write_buf = buf;
  ch->write_count = count;

  curl_easy_setopt (ch->c, CURLOPT_HTTPGET, 1L);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (ch->c, CURLOPT_RANGE, range);

  /* Send the command to the worker thread and wait. */
  struct command cmd = {
    .type = EASY_HANDLE,
    .ch = ch,
  };

  r = send_command_to_worker_and_wait (&cmd);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "pread");
    goto err;
  }
  update_times (ch->c);

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (ch->write_count == 0);

  free_handle (ch);
  return 0;

 err:
  if (ch)
    free_handle (ch);
  return -1;
}

/* NB: The terminology used by libcurl is confusing!
 *
 * WRITEFUNCTION / write_cb is used when reading from the remote server
 * READFUNCTION / read_cb is used when writing to the remote server.
 *
 * We use the same terminology as libcurl here.
 */
static size_t
write_cb (char *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *ch = opaque;
  /* XXX We should check this does not overflow. */
  size_t orig_realsize = size * nmemb;
  size_t realsize = orig_realsize;

  assert (ch->write_buf);

  /* Don't read more than the requested amount of data, even if the
   * server or libcurl sends more.
   */
  if (realsize > ch->write_count)
    realsize = ch->write_count;

  memcpy (ch->write_buf, ptr, realsize);

  ch->write_count -= realsize;
  ch->write_buf += realsize;

  return orig_realsize;
}

/* Write data to the remote server. */
static size_t read_cb (void *ptr, size_t size, size_t nmemb, void *opaque);

static int
curl_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset)
{
  CURLcode r;
  struct curl_handle *ch;
  char range[128];

  /* Get a curl easy handle. */
  ch = allocate_handle ();
  if (ch == NULL) goto err;

  /* Run the scripts if necessary and set headers in the handle. */
  if (do_scripts (ch) == -1) goto err;

  /* Tell the read_cb where we want the data to be read from.  read_cb
   * will update this if the data comes in multiple sections.
   */
  curl_easy_setopt (ch->c, CURLOPT_READFUNCTION, read_cb);
  curl_easy_setopt (ch->c, CURLOPT_READDATA, ch);
  ch->read_buf = buf;
  ch->read_count = count;

  curl_easy_setopt (ch->c, CURLOPT_UPLOAD, 1L);

  /* Make an HTTP range request. */
  snprintf (range, sizeof range, "%" PRIu64 "-%" PRIu64,
            offset, offset + count);
  curl_easy_setopt (ch->c, CURLOPT_RANGE, range);

  /* Send the command to the worker thread and wait. */
  struct command cmd = {
    .type = EASY_HANDLE,
    .ch = ch,
  };

  r = send_command_to_worker_and_wait (&cmd);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "pwrite");
    goto err;
  }
  update_times (ch->c);

  /* Could use curl_easy_getinfo here to obtain further information
   * about the connection.
   */

  /* As far as I understand the cURL API, this should never happen. */
  assert (ch->read_count == 0);

  free_handle (ch);
  return 0;

 err:
  if (ch)
    free_handle (ch);
  return -1;
}

static size_t
read_cb (void *ptr, size_t size, size_t nmemb, void *opaque)
{
  struct curl_handle *ch = opaque;
  /* XXX We should check this does not overflow. */
  size_t realsize = size * nmemb;

  assert (ch->read_buf);
  if (realsize > ch->read_count)
    realsize = ch->read_count;

  memcpy (ptr, ch->read_buf, realsize);

  ch->read_count -= realsize;
  ch->read_buf += realsize;

  return realsize;
}

static struct nbdkit_plugin plugin = {
  .name              = "curl",
  .version           = PACKAGE_VERSION,
  .load              = curl_load,
  .unload            = curl_unload,
  .config            = curl_config,
  .config_complete   = curl_config_complete,
  /* We can't set this here because of an obscure corner of the C
   * language.  "error: initializer element is not constant".  See
   * https://stackoverflow.com/questions/3025050
   */
  //.config_help       = curl_config_help,
  .magic_config_key  = "url",
  .dump_plugin       = curl_dump_plugin,
  .get_ready         = curl_get_ready,
  .after_fork        = curl_after_fork,
  .open              = curl_open,
  .close             = curl_close,
  .get_size          = curl_get_size,
  .can_multi_conn    = curl_can_multi_conn,
  .pread             = curl_pread,
  .pwrite            = curl_pwrite,
};

static void set_help (void) __attribute__ ((constructor));
static void
set_help (void)
{
  plugin.config_help = curl_config_help;
}

NBDKIT_REGISTER_PLUGIN (plugin)
