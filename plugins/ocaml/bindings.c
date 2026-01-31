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
#include <string.h>
#include <errno.h>

#include <caml/alloc.h>
#include <caml/bigarray.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>
#include <caml/threads.h>
#include <caml/unixsupport.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "plugin.h"

/* Bindings for miscellaneous nbdkit_* utility functions. */

/* NB: noalloc function. */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_error (value nv)
{
  nbdkit_set_error (code_of_unix_error (nv));
  return Val_unit;
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_parse_size (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  int64_t r;

  r = nbdkit_parse_size (String_val (strv));
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_size");
  rv = caml_copy_int64 (r);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_parse_probability (value whatv, value strv)
{
  CAMLparam2 (whatv, strv);
  CAMLlocal1 (dv);
  int r;
  double d;

  r = nbdkit_parse_probability (String_val (whatv), String_val (strv), &d);
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_probability");
  dv = caml_copy_double (d);

  CAMLreturn (dv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_parse_bool (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  int r;

  r = nbdkit_parse_bool (String_val (strv));
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_bool");
  rv = Val_bool (r);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_parse_delay (value whatv, value strv)
{
  CAMLparam2 (whatv, strv);
  CAMLlocal1 (rv);
  unsigned sec, nsec;
  int r;

  r = nbdkit_parse_delay (String_val (whatv), String_val (strv),
                          &sec, &nsec);
  if (r == -1)
    caml_invalid_argument ("nbdkit_parse_delay");
  rv = caml_alloc (2, 0);
  Store_field (rv, 0, Val_int (sec));
  Store_field (rv, 1, Val_int (nsec));

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_read_password (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  char *password;
  int r;

  r = nbdkit_read_password (String_val (strv), &password);
  if (r == -1)
    caml_invalid_argument ("nbdkit_read_password");
  rv = caml_copy_string (password);
  free (password);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_stdio_safe (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  int r;

  r = nbdkit_stdio_safe ();
  if (r == -1)
    caml_invalid_argument ("nbdkit_stdio_safe");
  rv = Val_bool (r);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_realpath (value strv)
{
  CAMLparam1 (strv);
  CAMLlocal1 (rv);
  char *ret;

  ret = nbdkit_realpath (String_val (strv));
  if (ret == NULL)
    caml_failwith ("nbdkit_realpath");
  rv = caml_copy_string (ret);
  free (ret);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_nanosleep (value secv, value nsecv)
{
  CAMLparam2 (secv, nsecv);
  int r;

  caml_enter_blocking_section ();
  r = nbdkit_nanosleep (Int_val (secv), Int_val (nsecv));
  caml_leave_blocking_section ();
  if (r == -1)
    caml_failwith ("nbdkit_nanosleep");

  CAMLreturn (Val_unit);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_export_name (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  const char *ret;

  ret = nbdkit_export_name ();
  /* Note that NULL indicates error.  Default export name is [""] even
   * for oldstyle.
   */
  if (ret == NULL)
    caml_failwith ("nbdkit_export_name");
  rv = caml_copy_string (ret);

  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_is_tls (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  int r;

  r = nbdkit_is_tls ();
  if (r == -1)
    caml_invalid_argument ("nbdkit_is_tls");
  rv = Val_bool (r);

  CAMLreturn (rv);
}

/* NB: noalloc function. */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_shutdown (value unitv)
{
  CAMLparam1 (unitv);

  nbdkit_shutdown ();
  CAMLreturn (Val_unit);
}

/* NB: noalloc function. */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_disconnect (value boolv)
{
  CAMLparam1 (boolv);

  nbdkit_disconnect (Bool_val (boolv));
  CAMLreturn (Val_unit);
}

/* NB: noalloc function. */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_debug (value strv)
{
  nbdkit_debug ("%s", String_val (strv));

  return Val_unit;
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_debug_hexdump (value bufv, value optprefixv, value startv)
{
  CAMLparam3 (bufv, optprefixv, startv);
  struct caml_ba_array *buf = Caml_ba_array_val (bufv);
  const void *data = buf->data;
  const size_t len = caml_ba_byte_size (buf);
  const char *prefix =
    optprefixv == Val_int (0)
    ? NULL /* None */
    : String_val (Field (optprefixv, 0)) /* Some prefix */;
  const uint64_t start = Int64_val (startv);

  nbdkit_debug_hexdump (data, len, prefix, start);
  CAMLreturn (Val_unit);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_debug_hexdiff (value buf1v, value buf2v,
                            value optprefixv, value startv)
{
  CAMLparam4 (buf1v, buf2v, optprefixv, startv);
  struct caml_ba_array *buf1 = Caml_ba_array_val (buf1v);
  struct caml_ba_array *buf2 = Caml_ba_array_val (buf2v);
  const void *data1 = buf1->data;
  const void *data2 = buf2->data;
  const size_t len1 = caml_ba_byte_size (buf1);
  const size_t len2 = caml_ba_byte_size (buf2);
  const char *prefix =
    optprefixv == Val_int (0)
    ? NULL /* None */
    : String_val (Field (optprefixv, 0)) /* Some prefix */;
  const uint64_t start = Int64_val (startv);

  if (len1 != len2)
    caml_invalid_argument ("nbdkit_debug_hexdiff: "
                           "buffers must have the same length");

  nbdkit_debug_hexdiff (data1, data2, len1, prefix, start);
  CAMLreturn (Val_unit);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_timestamp (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  const char *timestamp = nbdkit_timestamp ();
  rv = caml_copy_string (timestamp); /* timestamp is never NULL */
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_version (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);

  rv = caml_copy_string (PACKAGE_VERSION);
  CAMLreturn (rv);
}

/* NB: noalloc function. */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_api_version (value unitv)
{
  return Val_int (NBDKIT_API_VERSION);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_name (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal2 (nv, rv);
  const char *name = nbdkit_name ();

  /* Note that name == NULL is not an error.  So we construct a
   * string option here.
   */
  if (name == NULL)
    rv = Val_int (0);           /* None */
  else {
    nv = caml_copy_string (name);
    rv = caml_alloc (1, 0);     /* Some name */
    Field (rv, 0) = nv;
  }
  CAMLreturn (rv);
}

#ifdef HAVE_CAML_SOCKETADDR_H

#include <caml/socketaddr.h>

#ifndef HAVE_CAML_UNIX_ALLOC_SOCKADDR
#define caml_unix_alloc_sockaddr alloc_sockaddr /* OCaml <= 4.14 */
#endif

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_name (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  struct sockaddr_storage sa;
  socklen_t len = sizeof sa;
  int r;

  r = nbdkit_peer_name ((struct sockaddr *) &sa, &len);
  if (r == -1) caml_failwith ("nbdkit_peer_name");

  rv = caml_unix_alloc_sockaddr ((void *) &sa, len, -1);

  CAMLreturn (rv);
}

#else /* !HAVE_CAML_SOCKETADDR_H */

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_name (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  caml_failwith ("nbdkit_peer_name is not supported by this version of OCaml");
}

#endif /* !HAVE_CAML_SOCKETADDR_H */

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_pid (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  int64_t id = nbdkit_peer_pid ();
  if (id == -1) caml_failwith ("nbdkit_peer_pid");
  rv = caml_copy_int64 (id);
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_uid (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  int64_t id = nbdkit_peer_uid ();
  if (id == -1) caml_failwith ("nbdkit_peer_uid");
  rv = caml_copy_int64 (id);
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_gid (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  int64_t id = nbdkit_peer_gid ();
  if (id == -1) caml_failwith ("nbdkit_peer_gid");
  rv = caml_copy_int64 (id);
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_security_context (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  char *label = nbdkit_peer_security_context ();
  if (label == NULL) caml_failwith ("nbdkit_peer_security_context");
  rv = caml_copy_string (label);
  free (label);
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_tls_dn (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  char *label = nbdkit_peer_tls_dn ();
  if (label == NULL) caml_failwith ("nbdkit_peer_tls_dn");
  rv = caml_copy_string (label);
  free (label);
  CAMLreturn (rv);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_peer_tls_issuer_dn (value unitv)
{
  CAMLparam1 (unitv);
  CAMLlocal1 (rv);
  char *label = nbdkit_peer_tls_issuer_dn ();
  if (label == NULL) caml_failwith ("nbdkit_peer_tls_issuer_dn");
  rv = caml_copy_string (label);
  free (label);
  CAMLreturn (rv);
}
