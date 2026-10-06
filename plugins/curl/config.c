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

#include "cleanup.h"

#include "curldefs.h"

/* Plugin configuration. */
const char *url = NULL;         /* required */

static const char *cainfo = NULL;
static const char *capath = NULL;
static char *cookie = NULL;
static const char *cookiefile = NULL;
static const char *cookiejar = NULL;
static bool followlocation = true;
static struct curl_slist *headers = NULL;
static long http_version = CURL_HTTP_VERSION_NONE;
static long ipresolve = CURL_IPRESOLVE_WHATEVER;
static char *password = NULL;
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
static long protocols = CURLPROTO_ALL;
#else
static const char *protocols = NULL;
#endif
static const char *proxy = NULL;
static const char *proxy_cainfo = NULL;
static const char *proxy_capath = NULL;
static char *proxy_password = NULL;
static const char *proxy_user = NULL;
static struct curl_slist *resolves = NULL;
static bool sslverify = true;
static const char *ssl_cipher_list = NULL;
static long ssl_version = CURL_SSLVERSION_DEFAULT;
static const char *tls13_ciphers = NULL;
static bool tcp_keepalive = false;
static bool tcp_nodelay = true;
static uint32_t timeout = 0;
static const char *unix_socket_path = NULL;
static const char *user = NULL;
static const char *user_agent = NULL;

static int debug_cb (CURL *handle, curl_infotype type,
                     const char *data, size_t size, void *);

/* Use '-D curl.verbose=1' to set. */
NBDKIT_DLL_PUBLIC int curl_debug_verbose = 0;

/* Use '-D curl.verbose.ids=1' to set. */
NBDKIT_DLL_PUBLIC int curl_debug_verbose_ids = 0;

void
config_unload (void)
{
  free (cookie);
  if (headers)
    curl_slist_free_all (headers);
  free (password);
  free (proxy_password);
  if (resolves)
    curl_slist_free_all (resolves);
}

/* Add extra information to 'nbdkit curl --dump-plugin' output. */
void
curl_dump_plugin (void)
{
  /* The static version we were compiled against. */
  printf ("curl_version=%s\n", LIBCURL_VERSION);

  const curl_version_info_data *ver = curl_version_info (CURLVERSION_NOW);
  const char *const *p;
  /* The dynamic version we are linked against. */
  printf ("curl_dynamic_version=%s\n", ver->version);
  if (ver->ssl_version)
    printf ("curl_ssl_version=%s\n", ver->ssl_version);
  /* List of protocols supported. */
  for (p = ver->protocols; *p != NULL; p++)
    printf ("curl_protocol_%s=yes\n", *p);
}

#ifndef HAVE_CURLOPT_PROTOCOLS_STR
/* See <curl/curl.h> */
static struct { const char *name; long bitmask; } curl_protocols[] = {
  { "http", CURLPROTO_HTTP },
  { "https", CURLPROTO_HTTPS },
  { "ftp", CURLPROTO_FTP },
  { "ftps", CURLPROTO_FTPS },
  { "scp", CURLPROTO_SCP },
  { "sftp", CURLPROTO_SFTP },
  { "telnet", CURLPROTO_TELNET },
  { "ldap", CURLPROTO_LDAP },
  { "ldaps", CURLPROTO_LDAPS },
  { "dict", CURLPROTO_DICT },
  { "file", CURLPROTO_FILE },
  { "tftp", CURLPROTO_TFTP },
  { "imap", CURLPROTO_IMAP },
  { "imaps", CURLPROTO_IMAPS },
  { "pop3", CURLPROTO_POP3 },
  { "pop3s", CURLPROTO_POP3S },
  { "smtp", CURLPROTO_SMTP },
  { "smtps", CURLPROTO_SMTPS },
  { "rtsp", CURLPROTO_RTSP },
  { "rtmp", CURLPROTO_RTMP },
  { "rtmpt", CURLPROTO_RTMPT },
  { "rtmpe", CURLPROTO_RTMPE },
  { "rtmpte", CURLPROTO_RTMPTE },
  { "rtmps", CURLPROTO_RTMPS },
  { "rtmpts", CURLPROTO_RTMPTS },
  { "gopher", CURLPROTO_GOPHER },
#ifdef CURLPROTO_SMB
  { "smb", CURLPROTO_SMB },
#endif
#ifdef CURLPROTO_SMBS
  { "smbs", CURLPROTO_SMBS },
#endif
#ifdef CURLPROTO_MQTT
  { "mqtt", CURLPROTO_MQTT },
#endif
#ifdef CURLPROTO_GOPHERS
  { "gophers", CURLPROTO_GOPHERS },
#endif
  { NULL }
};

/* Parse the protocols parameter. */
static int
parse_protocols (const char *value)
{
  size_t n, i;

  protocols = 0;

  while (*value) {
    n = strcspn (value, ",");
    for (i = 0; curl_protocols[i].name != NULL; ++i) {
      if (strlen (curl_protocols[i].name) == n &&
          strncmp (value, curl_protocols[i].name, n) == 0) {
        protocols |= curl_protocols[i].bitmask;
        goto found;
      }
    }
    nbdkit_error ("protocols: protocol name not found: %.*s", (int) n, value);
    return -1;

  found:
    value += n;
    if (*value == ',')
      value++;
  }

  if (protocols == 0) {
    nbdkit_error ("protocols: empty list of protocols is not allowed");
    return -1;
  }

  nbdkit_debug ("curl: protocols: %ld", protocols);

  return 0;
}
#endif /* !HAVE_CURLOPT_PROTOCOLS_STR */

/* Called for each key=value passed on the command line. */
int
curl_config (const char *key, const char *value)
{
  int r;

  if (strcmp (key, "cainfo") == 0) {
    cainfo = value;
  }

  else if (strcmp (key, "capath") == 0) {
    capath = value;
  }

  else if (strcmp (key, "connections") == 0) {
    if (nbdkit_parse_unsigned ("connections", value,
                               &connections) == -1)
      return -1;
    if (connections == 0) {
      nbdkit_error ("connections parameter must not be 0");
      return -1;
    }
  }

  else if (strcmp (key, "cookie") == 0) {
    free (cookie);
    if (nbdkit_read_password (value, &cookie) == -1)
      return -1;
  }

  else if (strcmp (key, "cookiefile") == 0) {
    /* Reject cookiefile=- because it will cause libcurl to try to
     * read from stdin when we connect.
     */
    if (strcmp (value, "-") == 0) {
      nbdkit_error ("cookiefile parameter cannot be \"-\"");
      return -1;
    }
    cookiefile = value;
  }

  else if (strcmp (key, "cookiejar") == 0) {
    /* Reject cookiejar=- because it will cause libcurl to try to
     * write to stdout.
     */
    if (strcmp (value, "-") == 0) {
      nbdkit_error ("cookiejar parameter cannot be \"-\"");
      return -1;
    }
    cookiejar = value;
  }

  else if (strcmp (key, "cookie-script") == 0) {
    cookie_script = value;
  }

  else if (strcmp (key, "cookie-script-renew") == 0) {
    if (nbdkit_parse_unsigned ("cookie-script-renew", value,
                               &cookie_script_renew) == -1)
      return -1;
  }

  else if (strcmp (key, "followlocation") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    followlocation = r;
  }

  else if (strcmp (key, "header") == 0) {
    headers = curl_slist_append (headers, value);
    if (headers == NULL) {
      nbdkit_error ("curl_slist_append: %m");
      return -1;
    }
  }

  else if (strcmp (key, "header-script") == 0) {
    header_script = value;
  }

  else if (strcmp (key, "header-script-renew") == 0) {
    if (nbdkit_parse_unsigned ("header-script-renew", value,
                               &header_script_renew) == -1)
      return -1;
  }

  else if (strcmp (key, "http-version") == 0) {
    if (strcmp (value, "none") == 0)
      http_version = CURL_HTTP_VERSION_NONE;
    else if (strcmp (value, "1.0") == 0)
      http_version = CURL_HTTP_VERSION_1_0;
    else if (strcmp (value, "1.1") == 0)
      http_version = CURL_HTTP_VERSION_1_1;
#ifdef HAVE_CURL_HTTP_VERSION_2_0
    else if (strcmp (value, "2.0") == 0)
      http_version = CURL_HTTP_VERSION_2_0;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_2TLS
    else if (strcmp (value, "2TLS") == 0)
      http_version = CURL_HTTP_VERSION_2TLS;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE
    else if (strcmp (value, "2-prior-knowledge") == 0)
      http_version = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_3
    else if (strcmp (value, "3") == 0)
      http_version = CURL_HTTP_VERSION_3;
#endif
#ifdef HAVE_CURL_HTTP_VERSION_3ONLY
    else if (strcmp (value, "3only") == 0)
      http_version = CURL_HTTP_VERSION_3ONLY;
#endif
    else {
      nbdkit_error ("unknown http-version: %s", value);
      return -1;
    }
  }

  else if (strcmp (key, "ipresolve") == 0) {
    if (strcmp (value, "any") == 0 || strcmp (value, "whatever") == 0)
      ipresolve = CURL_IPRESOLVE_WHATEVER;
    else if (strcmp (value, "v4") == 0 || strcmp (value, "4") == 0)
      ipresolve = CURL_IPRESOLVE_V4;
    else if (strcmp (value, "v6") == 0 || strcmp (value, "6") == 0)
      ipresolve = CURL_IPRESOLVE_V6;
    else {
      nbdkit_error ("unknown ipresolve: %s", value);
      return -1;
    }
  }

  else if (strcmp (key, "password") == 0) {
    free (password);
    if (nbdkit_read_password (value, &password) == -1)
      return -1;
  }

  else if (strcmp (key, "protocols") == 0) {
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
    if (parse_protocols (value) == -1)
      return -1;
#else
    protocols = value;
#endif
  }

  else if (strcmp (key, "proxy") == 0) {
    proxy = value;
  }

  else if (strcmp (key, "proxy-cainfo") == 0) {
    proxy_cainfo = value;
  }

  else if (strcmp (key, "proxy-capath") == 0) {
    proxy_capath = value;
  }

  else if (strcmp (key, "proxy-password") == 0) {
    free (proxy_password);
    if (nbdkit_read_password (value, &proxy_password) == -1)
      return -1;
  }

  else if (strcmp (key, "proxy-user") == 0)
    proxy_user = value;

  else if (strcmp (key, "resolve") == 0) {
    resolves = curl_slist_append (headers, value);
    if (resolves == NULL) {
      nbdkit_error ("curl_slist_append: %m");
      return -1;
    }
  }

  else if (strcmp (key, "sslverify") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    sslverify = r;
  }

  else if (strcmp (key, "ssl-version") == 0) {
    if (strcmp (value, "default") == 0)
      ssl_version = CURL_SSLVERSION_DEFAULT;
    else if (strcmp (value, "tlsv1") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1;
    else if (strcmp (value, "sslv2") == 0)
      ssl_version = CURL_SSLVERSION_SSLv2;
    else if (strcmp (value, "sslv3") == 0)
      ssl_version = CURL_SSLVERSION_SSLv3;
    else if (strcmp (value, "tlsv1.0") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_0;
    else if (strcmp (value, "tlsv1.1") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_1;
    else if (strcmp (value, "tlsv1.2") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_2;
    else if (strcmp (value, "tlsv1.3") == 0)
      ssl_version = CURL_SSLVERSION_TLSv1_3;
#ifdef CURL_SSLVERSION_MAX_DEFAULT
    else if (strcmp (value, "max-default") == 0)
      ssl_version = CURL_SSLVERSION_MAX_DEFAULT;
#endif
#ifdef CURL_SSLVERSION_MAX_TLSv1_0
    else if (strcmp (value, "max-tlsv1.0") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_0;
#endif
#ifdef CURL_SSLVERSION_MAX_TLSv1_1
    else if (strcmp (value, "max-tlsv1.1") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_1;
#endif
#ifdef CURL_SSLVERSION_MAX_TLSv1_2
    else if (strcmp (value, "max-tlsv1.2") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_2;
#endif
#ifdef CURL_SSLVERSION_MAX_TLSv1_3
    else if (strcmp (value, "max-tlsv1.3") == 0)
      ssl_version = CURL_SSLVERSION_MAX_TLSv1_3;
#endif
    else {
      nbdkit_error ("unknown ssl-version: %s", value);
      return -1;
    }
  }

  else if (strcmp (key, "ssl-cipher-list") == 0)
    ssl_cipher_list = value;

  else if (strcmp (key, "tls13-ciphers") == 0)
    tls13_ciphers = value;

  else if (strcmp (key, "tcp-keepalive") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    tcp_keepalive = r;
  }

  else if (strcmp (key, "tcp-nodelay") == 0) {
    r = nbdkit_parse_bool (value);
    if (r == -1)
      return -1;
    tcp_nodelay = r;
  }

  else if (strcmp (key, "timeout") == 0) {
    if (nbdkit_parse_uint32_t ("timeout", value, &timeout) == -1)
      return -1;
#if LONG_MAX < UINT32_MAX
    /* C17 5.2.4.2.1 requires that LONG_MAX is at least 2^31 - 1.
     * However a large positive number might still exceed the limit.
     */
    if (timeout > LONG_MAX) {
      nbdkit_error ("timeout is too large");
      return -1;
    }
#endif
  }

  else if (strcmp (key, "unix-socket-path") == 0 ||
           strcmp (key, "unix_socket_path") == 0)
    unix_socket_path = value;

  else if (strcmp (key, "url") == 0)
    url = value;

  else if (strcmp (key, "user") == 0)
    user = value;

  else if (strcmp (key, "user-agent") == 0)
    user_agent = value;

  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

/* Check the user did pass a url parameter. */
int
curl_config_complete (void)
{
  if (url == NULL) {
    nbdkit_error ("you must supply the url=<URL> parameter "
                  "after the plugin name on the command line");
    return -1;
  }

  if (headers && header_script) {
    nbdkit_error ("header and header-script cannot be used at the same time");
    return -1;
  }

  if (!header_script && header_script_renew) {
    nbdkit_error ("header-script-renew cannot be used without header-script");
    return -1;
  }

  if (cookie && cookie_script) {
    nbdkit_error ("cookie and cookie-script cannot be used at the same time");
    return -1;
  }

  if (!cookie_script && cookie_script_renew) {
    nbdkit_error ("cookie-script-renew cannot be used without cookie-script");
    return -1;
  }

  return 0;
}

const char *curl_config_help =
  "cainfo=<CAINFO>            Path to Certificate Authority file.\n"
  "capath=<CAPATH>            Path to directory with CA certificates.\n"
  "connections=<N>            Number of HTTP connections to use.\n"
  "cookie=<COOKIE>            Set HTTP/HTTPS cookies.\n"
  "cookiefile=                Enable cookie processing.\n"
  "cookiefile=<FILENAME>      Read cookies from file.\n"
  "cookiejar=<FILENAME>       Read and write cookies to jar.\n"
  "cookie-script=<SCRIPT>     Script to set HTTP/HTTPS cookies.\n"
  "cookie-script-renew=<SECS> Time to renew HTTP/HTTPS cookies.\n"
  "followlocation=false       Do not follow redirects.\n"
  "header=<HEADER>            Set HTTP/HTTPS header.\n"
  "header-script=<SCRIPT>     Script to set HTTP/HTTPS headers.\n"
  "header-script-renew=<SECS> Time to renew HTTP/HTTPS headers.\n"
  "http-version=none|...      Force a particular HTTP protocol.\n"
  "ipresolve=any|v4|v6        Force IPv4 or IPv6.\n"
  "password=<PASSWORD>        The password for the user account.\n"
  "protocols=PROTO,PROTO,..   Limit protocols allowed.\n"
  "proxy=<PROXY>              Set proxy URL.\n"
  "proxy-cainfo=<CAINFO>      Path to Certificate Authority file for proxy.\n"
  "proxy-capath=<CAPATH>      Path to directory with CA certs for proxy.\n"
  "proxy-password=<PASSWORD>  The proxy password.\n"
  "proxy-user=<USER>          The proxy user.\n"
  "resolve=<HOST>:<PORT>:<ADDR> Custom host to IP address resolution.\n"
  "sslverify=false            Do not verify SSL certificate of remote host.\n"
  "ssl-cipher-list=C1:C2:..   Specify TLS/SSL cipher suites to be used.\n"
  "ssl-version=<VERSION>      Specify preferred TLS/SSL version.\n"
  "tcp-keepalive=true         Enable TCP keepalives.\n"
  "tcp-nodelay=false          Disable Nagle’s algorithm.\n"
  "timeout=<TIMEOUT>          Set the timeout for requests (seconds).\n"
  "tls13-ciphers=C1:C2:..     Specify TLS 1.3 cipher suites to be used.\n"
  "unix-socket-path=<PATH>    Open Unix domain socket instead of TCP/IP.\n"
  "url=<URL>       (required) The disk image URL to serve.\n"
  "user=<USER>                The user to log in as.\n"
  "user-agent=<USER-AGENT>    Send user-agent header for HTTP/HTTPS."
  ;

/* Allocate and initialize a new libcurl handle. */
struct curl_handle *
allocate_handle (void)
{
  struct curl_handle *ch;
  CURLcode r;

  ch = calloc (1, sizeof *ch);
  if (ch == NULL) {
    nbdkit_error ("calloc: %m");
    free (ch);
    return NULL;
  }

  ch->c = curl_easy_init ();
  if (ch->c == NULL) {
    nbdkit_error ("curl_easy_init: failed: %m");
    goto err;
  }

  /* Set the private data pointer of the easy handle to point to our
   * containing struct curl_handle.  This can be retrieved at any time
   * using 'curl_easy_getinfo (c, CURLINFO_PRIVATE, &ch)'.
   */
  curl_easy_setopt (ch->c, CURLOPT_PRIVATE, ch);

  if (curl_debug_verbose) {
    /* NB: Constants must be explicitly long because the parameter is
     * varargs.
     */
    curl_easy_setopt (ch->c, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt (ch->c, CURLOPT_DEBUGFUNCTION, debug_cb);
  }

  curl_easy_setopt (ch->c, CURLOPT_ERRORBUFFER, ch->errbuf);

  if (unix_socket_path) {
    r = curl_easy_setopt (ch->c, CURLOPT_UNIX_SOCKET_PATH, unix_socket_path);
    if (r != CURLE_OK) {
      display_curl_error (ch, r, "curl_easy_setopt: CURLOPT_UNIX_SOCKET_PATH");
      goto err;
    }
  }

  /* Set the URL. */
  r = curl_easy_setopt (ch->c, CURLOPT_URL, url);
  if (r != CURLE_OK) {
    display_curl_error (ch, r, "curl_easy_setopt: CURLOPT_URL [%s]", url);
    goto err;
  }

  /* Various options we always set.
   *
   * NB: Both here and below constants must be explicitly long because
   * the parameter is varargs.
   *
   * For use of CURLOPT_NOSIGNAL see:
   * https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html
   */
  curl_easy_setopt (ch->c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (ch->c, CURLOPT_AUTOREFERER, 1L);
  if (followlocation)
    curl_easy_setopt (ch->c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (ch->c, CURLOPT_FAILONERROR, 1L);

  /* Options. */
  if (cainfo) {
    if (strlen (cainfo) == 0)
      curl_easy_setopt (ch->c, CURLOPT_CAINFO, NULL);
    else
      curl_easy_setopt (ch->c, CURLOPT_CAINFO, cainfo);
  }
  if (capath)
    curl_easy_setopt (ch->c, CURLOPT_CAPATH, capath);
  if (cookie)
    curl_easy_setopt (ch->c, CURLOPT_COOKIE, cookie);
  if (cookiefile)
    curl_easy_setopt (ch->c, CURLOPT_COOKIEFILE, cookiefile);
  if (cookiejar)
    curl_easy_setopt (ch->c, CURLOPT_COOKIEJAR, cookiejar);
  if (headers)
    curl_easy_setopt (ch->c, CURLOPT_HTTPHEADER, headers);
  if (http_version != CURL_HTTP_VERSION_NONE)
    curl_easy_setopt (ch->c, CURLOPT_HTTP_VERSION, (long) http_version);
  if (ipresolve != CURL_IPRESOLVE_WHATEVER)
    curl_easy_setopt (ch->c, CURLOPT_IPRESOLVE, (long) ipresolve);

  if (password)
    curl_easy_setopt (ch->c, CURLOPT_PASSWORD, password);
#ifndef HAVE_CURLOPT_PROTOCOLS_STR
  if (protocols != CURLPROTO_ALL) {
    curl_easy_setopt (ch->c, CURLOPT_PROTOCOLS, protocols);
    curl_easy_setopt (ch->c, CURLOPT_REDIR_PROTOCOLS, protocols);
  }
#else /* HAVE_CURLOPT_PROTOCOLS_STR (new in 7.85.0) */
  if (protocols) {
    curl_easy_setopt (ch->c, CURLOPT_PROTOCOLS_STR, protocols);
    curl_easy_setopt (ch->c, CURLOPT_REDIR_PROTOCOLS_STR, protocols);
  }
#endif /* HAVE_CURLOPT_PROTOCOLS_STR */
  if (proxy)
    curl_easy_setopt (ch->c, CURLOPT_PROXY, proxy);
  if (proxy_cainfo) {
    if (strlen (proxy_cainfo) == 0)
      curl_easy_setopt (ch->c, CURLOPT_PROXY_CAINFO, NULL);
    else
      curl_easy_setopt (ch->c, CURLOPT_PROXY_CAINFO, proxy_cainfo);
  }
  if (proxy_capath)
    curl_easy_setopt (ch->c, CURLOPT_PROXY_CAPATH, proxy_capath);
  if (proxy_password)
    curl_easy_setopt (ch->c, CURLOPT_PROXYPASSWORD, proxy_password);
  if (proxy_user)
    curl_easy_setopt (ch->c, CURLOPT_PROXYUSERNAME, proxy_user);
  if (!sslverify) {
    curl_easy_setopt (ch->c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt (ch->c, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (resolves)
    curl_easy_setopt (ch->c, CURLOPT_RESOLVE, resolves);
  if (ssl_version != CURL_SSLVERSION_DEFAULT)
    curl_easy_setopt (ch->c, CURLOPT_SSLVERSION, (long) ssl_version);
  if (ssl_cipher_list)
    curl_easy_setopt (ch->c, CURLOPT_SSL_CIPHER_LIST, ssl_cipher_list);
  if (tls13_ciphers) {
#if (LIBCURL_VERSION_MAJOR > 7) || \
    (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 61)
    curl_easy_setopt (ch->c, CURLOPT_TLS13_CIPHERS, tls13_ciphers);
#else
    /* This is not available before curl-7.61 */
    nbdkit_error ("tls13-ciphers is not supported in this build of "
                  "nbdkit-curl-plugin");
    goto err;
#endif
  }
  if (tcp_keepalive)
    curl_easy_setopt (ch->c, CURLOPT_TCP_KEEPALIVE, 1L);
  if (!tcp_nodelay)
    curl_easy_setopt (ch->c, CURLOPT_TCP_NODELAY, 0L);
  if (timeout > 0)
    curl_easy_setopt (ch->c, CURLOPT_TIMEOUT, (long) timeout);
  if (user)
    curl_easy_setopt (ch->c, CURLOPT_USERNAME, user);
  if (user_agent)
    curl_easy_setopt (ch->c, CURLOPT_USERAGENT, user_agent);

  /* Get set up for reading and writing. */
  curl_easy_setopt (ch->c, CURLOPT_HEADERFUNCTION, NULL);
  curl_easy_setopt (ch->c, CURLOPT_HEADERDATA, NULL);

  return ch;

 err:
  if (ch->c)
    curl_easy_cleanup (ch->c);
  free (ch);
  return NULL;
}

void
free_handle (struct curl_handle *ch)
{
  curl_easy_cleanup (ch->c);
  if (ch->headers_copy)
    curl_slist_free_all (ch->headers_copy);
  free (ch);
}

/* When using CURLOPT_VERBOSE, this callback is used to redirect
 * messages to nbdkit_debug (instead of stderr).
 */
static int
debug_cb (CURL *handle, curl_infotype type,
          const char *data, size_t size, void *opaque)
{
  size_t origsize = size;
  CLEANUP_FREE char *str;
  curl_off_t conn_id = -1, xfer_id = -1;

#if defined(HAVE_CURLINFO_CONN_ID) && defined(HAVE_CURLINFO_XFER_ID)
  if (curl_debug_verbose_ids) {
    curl_easy_getinfo (handle, CURLINFO_CONN_ID, &conn_id);
    curl_easy_getinfo (handle, CURLINFO_XFER_ID, &xfer_id);
  }
#endif

  /* The data parameter passed is NOT \0-terminated, but also it may
   * have \n or \r\n line endings.  The only sane way to deal with
   * this is to copy the string.  (The data strings may also be
   * multi-line, but we don't deal with that here).
   */
  str = malloc (size + 1);
  if (str == NULL)
    goto out;
  memcpy (str, data, size);
  str[size] = '\0';

  while (size > 0 && (str[size-1] == '\n' || str[size-1] == '\r')) {
    str[size-1] = '\0';
    size--;
  }

  switch (type) {
  case CURLINFO_TEXT:
    if (conn_id >= 0 && xfer_id >= 0)
      nbdkit_debug ("conn %" PRIi64 " xfer %" PRIi64 ": %s",
                    conn_id, xfer_id, str);
    else
      nbdkit_debug ("%s",str);
    break;
  case CURLINFO_HEADER_IN:
    if (conn_id >= 0 && xfer_id >= 0)
      nbdkit_debug ("conn %" PRIi64 " xfer %" PRIi64 ": S: %s",
                    conn_id, xfer_id, str);
    else
      nbdkit_debug ("S: %s", str);
    break;
  case CURLINFO_HEADER_OUT:
    if (conn_id >= 0 && xfer_id >= 0)
      nbdkit_debug ("conn %" PRIi64 " xfer %" PRIi64 ": C: %s",
                    conn_id, xfer_id, str);
    else
      nbdkit_debug ("C: %s", str);
    break;
  default:
    /* Assume everything else is binary data that we cannot print. */
    if (conn_id >= 0 && xfer_id >= 0)
      nbdkit_debug ("conn %" PRIi64 " xfer %" PRIi64 ": "
                    "<data with size=%zu>",
                    conn_id, xfer_id,
                    origsize);
    else
      nbdkit_debug ("<data with size=%zu>", origsize);
  }

 out:
  return 0;
}
