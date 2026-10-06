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

#ifndef NBDKIT_CURLDEFS_H
#define NBDKIT_CURLDEFS_H

#include <stdbool.h>
#include <pthread.h>

#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#else
/* Some old platforms lack atomic types, but 32 bit ints are usually
 * "atomic enough".
 */
#define _Atomic /**/
#endif

#include "windows-compat.h"

/* Macro CURL_AT_LEAST_VERSION was added in 2015 (Curl 7.43) and we
 * require a more recent version of curl.
 */
#if CURL_AT_LEAST_VERSION (7, 66, 0)
#define HAVE_CURL_MULTI_POLL
#endif
#if CURL_AT_LEAST_VERSION (8, 2, 0)
#define HAVE_CURLINFO_CONN_ID
#define HAVE_CURLINFO_XFER_ID
#endif

extern const char *url;

extern unsigned connections;

extern const char *cookie_script;
extern unsigned cookie_script_renew;
extern const char *header_script;
extern unsigned header_script_renew;

extern int curl_debug_verbose;

/* The per-connection handle. */
struct handle {
  int readonly;
};

/* The libcurl handle and some associated fields and buffers. */
struct curl_handle {
  /* The underlying curl handle. */
  CURL *c;

  char errbuf[CURL_ERROR_SIZE];

  /* Before doing a read or write operation, set these to point to the
   * buffer where you want the data to be stored / come from.  Note
   * the confusing terminology from libcurl: write_* is used when
   * reading, read_* is used when writing.
   */
  char *write_buf;
  uint32_t write_count;
  const char *read_buf;
  uint32_t read_count;

  /* This field is used by curl_get_size. */
  bool accept_range;

  /* Used by scripts.c */
  struct curl_slist *headers_copy;

  /* Used by worker thread in worker.c */
  struct command *cmd;
};

/* Asynchronous commands that can be sent to the worker thread. */
enum command_type { EASY_HANDLE, STOP };
struct command {
  /* These fields are set by the caller. */
  enum command_type type;       /* command */
  struct curl_handle *ch;       /* for EASY_HANDLE, the easy handle */

  /* This field is set to a unique value by send_command_and_wait. */
  uint64_t id;                  /* serial number */

  /* These fields are used to signal back that the command finished. */
  pthread_mutex_t mutex;        /* completion mutex */
  pthread_cond_t cond;          /* completion condition */
  CURLcode status;              /* status code (CURLE_OK = succeeded) */
};

/* config.c */
extern int curl_config (const char *key, const char *value);
extern int curl_config_complete (void);
extern const char *curl_config_help;
extern void config_unload (void);
extern void curl_dump_plugin (void);
extern struct curl_handle *allocate_handle (void);
extern void free_handle (struct curl_handle *);

/* worker.c */
extern int worker_get_ready (void);
extern int worker_after_fork (void);
extern void worker_unload (void);
extern CURLcode send_command_to_worker_and_wait (struct command *cmd);

/* scripts.c */
extern int do_scripts (struct curl_handle *ch);
extern void scripts_unload (void);

/* times.c */
extern void update_times (CURL *c); /* called after every curl_easy_perform */
extern void display_times (void);

/* Translate CURLcode to nbdkit_error. */
#define display_curl_error(ch, r, fs, ...)                      \
  do {                                                          \
    nbdkit_error ((fs ": %s: %s"), ## __VA_ARGS__,              \
                  curl_easy_strerror ((r)), (ch)->errbuf);      \
  } while (0)

#endif /* NBDKIT_CURLDEFS_H */
