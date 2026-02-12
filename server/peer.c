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

/* Exported functions for reading peer information over Unix domain
 * sockets.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include <sys/types.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#include "internal.h"

/* Since nbdkit 1.46 we have declared various function parameters as
 * nonnull.  But for older plugins we still have runtime checks for
 * NULL.  GCC has a very unhelpful (I'd argue, incorrect) warning when
 * you do this.  Get rid of it.
 */
#if defined (__GNUC__) && NBDKIT_GCC_VERSION >= 120000 /* gcc >= 12.0 */
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif

NBDKIT_DLL_PUBLIC int
nbdkit_peer_name (struct sockaddr *addr, socklen_t *addrlen)
{
  struct connection *conn = threadlocal_get_conn ();
  int s;

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  s = conn->sockin;
  if (s == -1) {
    nbdkit_error ("socket not open");
    return -1;
  }

  if (getpeername (s, addr, addrlen) == -1) {
    nbdkit_error ("peername: %m");
    return -1;
  }

  return 0;
}

#if defined (SO_PEERCRED) && \
  (defined (HAVE_STRUCT_UCRED_UID) || defined (HAVE_STRUCT_SOCKPEERCRED_UID))

#define GET_PEERCRED_DEFINED 1

static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
#if HAVE_STRUCT_UCRED_UID
  struct ucred cred;
#elif HAVE_STRUCT_SOCKPEERCRED_UID
  /* The struct has a different name on OpenBSD, but the same members. */
  struct sockpeercred cred;
#endif
  socklen_t n = sizeof cred;

  if (getsockopt (s, SOL_SOCKET, SO_PEERCRED, &cred, &n) == -1) {
    nbdkit_error ("getsockopt: SO_PEERCRED: %m");
    return -1;
  }

  if (pid && cred.pid >= 1) {
#if SIZEOF_PID_T >= 8
    if (cred.pid > INT64_MAX)
      nbdkit_error ("pid out of range: cannot be mapped to int64_t");
    else
#endif
      *pid = cred.pid;
  }
  if (uid && cred.uid >= 0) {
#if SIZEOF_UID_T >= 8
    if (cred.uid > INT64_MAX)
      nbdkit_error ("uid out of range: cannot be mapped to int64_t");
    else
#endif
      *uid = cred.uid;
  }
  if (gid && cred.gid >= 0) {
#if SIZEOF_GID_T >= 8
    if (cred.gid > INT64_MAX)
      nbdkit_error ("gid out of range: cannot be mapped to int64_t");
    else
#endif
      *gid = cred.gid;
  }

  return 0;
}

#endif /* SO_PEERCRED */

#ifdef LOCAL_PEERCRED

#define GET_PEERCRED_DEFINED 1

/* FreeBSD supports LOCAL_PEERCRED and struct xucred. */
static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
  struct xucred xucred;
  socklen_t n = sizeof xucred;

  if (getsockopt (s, 0, LOCAL_PEERCRED, &xucred, &n) == -1) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: %m");
    return -1;
  }

  if (xucred.cr_version != XUCRED_VERSION) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: "
                  "struct xucred version (%u) "
                  "did not match expected version (%u)",
                  xucred.cr_version, XUCRED_VERSION);
    return -1;
  }

  if (n != sizeof xucred) {
    nbdkit_error ("getsockopt: LOCAL_PEERCRED: did not return full struct");
    return -1;
  }

  if (pid)
    nbdkit_error ("nbdkit_peer_pid is not supported on this platform");
  if (uid && xucred.cr_uid >= 0) {
#if SIZEOF_UID_T >= 8
    if (xucred.cr_uid <= INT64_MAX)
#endif
      *uid = xucred.cr_uid;
#if SIZEOF_UID_T >= 8
    else
      nbdkit_error ("uid out of range: cannot be mapped to int64_t");
#endif
  }
  if (gid && xucred.cr_ngroups > 0) {
#if SIZEOF_GID_T >= 8
    if (xucred.cr_gid <= INT64_MAX)
#endif
      *gid = xucred.cr_gid;
#if SIZEOF_GID_T >= 8
    else
      nbdkit_error ("gid out of range: cannot be mapped to int64_t");
#endif
  }

  return 0;
}

#endif /* LOCAL_PEERCRED */

#ifndef GET_PEERCRED_DEFINED

static int
get_peercred (int s, int64_t *pid, int64_t *uid, int64_t *gid)
{
  nbdkit_error ("nbdkit_peer_pid, nbdkit_peer_uid and nbdkit_peer_gid "
                "are not supported on this platform");
  return -1;
}

#endif

static int
get_peercred_common (int64_t *pid, int64_t *uid, int64_t *gid)
{
  struct connection *conn = threadlocal_get_conn ();
  int s;

  if (pid) *pid = -1;
  if (uid) *uid = -1;
  if (gid) *gid = -1;

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return -1;
  }

  s = conn->sockin;
  if (s == -1) {
    nbdkit_error ("socket not open");
    return -1;
  }

  return get_peercred (s, pid, uid, gid);
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_pid (void)
{
  int64_t pid;

  if (get_peercred_common (&pid, NULL, NULL) == -1)
    return -1;

  return pid;
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_uid (void)
{
  int64_t uid;

  if (get_peercred_common (NULL, &uid, NULL) == -1)
    return -1;

  return uid;
}

NBDKIT_DLL_PUBLIC int64_t
nbdkit_peer_gid (void)
{
  int64_t gid;

  if (get_peercred_common (NULL, NULL, &gid) == -1)
    return -1;

  return gid;
}

#ifdef SO_PEERSEC

NBDKIT_DLL_PUBLIC char *
nbdkit_peer_security_context (void)
{
  struct connection *conn = threadlocal_get_conn ();
  int s;
  char *label = NULL;
  socklen_t optlen = 0;
  int r;

  if (!conn) {
    nbdkit_error ("no connection in this thread");
    return NULL;
  }

  s = conn->sockin;
  if (s == -1) {
    nbdkit_error ("socket not open");
    return NULL;
  }

  /* Get the length of the label before allocating.  Note the ip(7)
   * manual implies that the level should be IPPROTO_IP for AF_INET
   * sockets.  However Linux always uses SOL_SOCKET.
   */
  errno = 0;
  r = getsockopt (conn->sockin, SOL_SOCKET, SO_PEERSEC, label, &optlen);
  if (r == 0) {
    /* Zero-length label probably. */
    label = calloc (1, 1);
    if (label == NULL) {
      nbdkit_error ("calloc: %m");
      return NULL;
    }
    return label; /* caller frees */
  }
  else if (r == -1 && errno == ENOPROTOOPT) {
    /* This is not really an error so don't call nbdkit_error. */
    debug ("getsockopt: SO_PEERSEC: %m");
    return NULL;
  }
  else if (r == -1 && errno != ERANGE) {
    nbdkit_error ("getsockopt: SO_PEERSEC: %m");
    return NULL;
  }

  /* It's not defined if Linux will NUL-terminate the returned string,
   * or even if it will completely fill the buffer, so we play it safe
   * here.
   */
  label = calloc (optlen+1, 1);
  if (label == NULL) {
    nbdkit_error ("calloc: %m");
    return NULL;
  }

  /* Read the label. */
  if (getsockopt (conn->sockin, SOL_SOCKET, SO_PEERSEC, label, &optlen) == -1) {
    nbdkit_error ("getsockopt: SO_PEERSEC: %m");
    free (label);
    return NULL;
  }

  return label; /* caller frees */
}

#else /* !SO_PEERSEC */

NBDKIT_DLL_PUBLIC char *
nbdkit_peer_security_context (void)
{
  nbdkit_error ("SO_PEERSEC is not available on this platform");
  return NULL;
}

#endif /* !SO_PEERSEC */
