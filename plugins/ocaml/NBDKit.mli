(* hey emacs, this is OCaml code: -*- tuareg -*- *)
(* nbdkit OCaml interface
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
 *)

(** Interface between plugins written in OCaml and the nbdkit server.

    @see <https://libguestfs.org/nbdkit-ocaml-plugin.3.html> nbdkit-ocaml-plugin(3)

    @see <https://libguestfs.org/nbdkit-plugin.3.html> nbdkit-plugin(3) *)

(** {2 Exception that plugins can raise}

    Optionally plugins can raise this exception to control how
    nbdkit_set_error and nbdkit_error are called. *)

exception Error of Unix.error option * string

(** {2 Flags passed from the server to various callbacks} *)

type flags = flag list
and flag = May_trim | FUA | Req_one

type fua_flag = FuaNone | FuaEmulate | FuaNative

type cache_flag = CacheNone | CacheEmulate | CacheNop

(** {2 Other types} *)

(** The type of the extent list returned by [~extents] *)
type extent = {
  offset : int64;
  length : int64;
  is_hole : bool;
  is_zero : bool;
}

(** The type of the export list returned by [~list_exports] *)
type export = {
  name : string;
  description : string option;
}

(** The type of the thread model returned by [~thread_model] *)
type thread_model =
| THREAD_MODEL_SERIALIZE_CONNECTIONS
| THREAD_MODEL_SERIALIZE_ALL_REQUESTS
| THREAD_MODEL_SERIALIZE_REQUESTS
| THREAD_MODEL_PARALLEL

(** {2 Buffer used for pread and pwrite} *)

type buf =
  (char, Bigarray.int8_unsigned_elt, Bigarray.c_layout) Bigarray.Array1.t
(** Define a convenient type name for the buffer type passed to
    the [~pread] and [~pwrite] methods of {!register_plugin}.

    As well as {!buf_len} and the blitting functions below, usual
    {!module:Bigarray.Array1} functions can be used. *)

val buf_len : buf -> int
(** Return the length of the [~pread] and [~pwrite] {!buf} parameter. *)

val buf_create : int -> buf
(** Create {!buf} of the given length.  The content is undefined. *)

val buf_zeroes : int -> buf
(** Create {!buf} of the given length containing all zero bytes. *)

val set_buf_to_zero : buf -> unit
(** Set all bytes in an existing buffer to zero. *)

val buf_of_string : string -> buf
val string_of_buf : buf -> string
val buf_of_bytes : bytes -> buf
val bytes_of_buf : buf -> bytes
(** Convenience functions to convert to and from string/bytes and buf.

    These are not very efficient.  Use {!module:Bigarray.Array1} functions
    instead for efficiency. *)

val blit_string_to_buf : string -> int -> buf -> int -> int -> unit
(** [blit_string_to_buf src src_pos buf buf_pos len] copies
    [len] bytes from string [src+src_pos] to [buf+buf_pos] *)

val blit_bytes_to_buf : bytes -> int -> buf -> int -> int -> unit
(** The same as above but the source is [bytes] *)

val blit_buf_to_bytes : buf -> int -> bytes -> int -> int -> unit
(** [blit_buf_to_bytes buf buf_pos dst dst_pos len] copies
    [len] bytes from [buf+buf_pos] to bytes [dst+dst_pos] *)

(** {2 Plugin} *)

val register_plugin :
  (* Plugin description. *)
  name: string ->
  ?longname: string ->
  ?version: string ->
  ?description: string ->

  (* Plugin lifecycle. *)
  ?load: (unit -> unit) ->
  ?get_ready: (unit -> unit) ->
  ?after_fork: (unit -> unit) ->
  ?cleanup: (unit -> unit) ->
  ?unload: (unit -> unit) ->

  (* Plugin configuration. *)
  ?config: (string -> string -> unit) ->
  ?config_complete: (unit -> unit) ->
  ?config_help: string ->
  ?thread_model: (unit -> thread_model) ->
  ?magic_config_key: string ->

  (* Connection lifecycle. *)
  ?preconnect: (bool -> unit) ->
  open_connection: (bool -> 'a) ->
  ?close: ('a -> unit) ->

  (* NBD negotiation. *)
  get_size: ('a -> int64) ->
  ?export_description: ('a -> string) ->
  ?block_size: ('a -> int * int * int64) ->
  ?can_cache: ('a -> cache_flag) ->
  ?can_extents: ('a -> bool) ->
  ?can_fast_zero: ('a -> bool) ->
  ?can_flush: ('a -> bool) ->
  ?can_fua: ('a -> fua_flag) ->
  ?can_multi_conn: ('a -> bool) ->
  ?can_trim: ('a -> bool) ->
  ?can_write: ('a -> bool) ->
  ?can_zero: ('a -> bool) ->
  ?is_rotational: ('a -> bool) ->

  (* Serving data. *)
  pread: ('a -> buf -> int64 -> flags -> unit) ->
  ?pwrite: ('a -> buf -> int64 -> flags -> unit) ->
  ?flush: ('a -> flags -> unit) ->
  ?trim: ('a -> int64 -> int64 -> flags -> unit) ->
  ?zero: ('a -> int64 -> int64 -> flags -> unit) ->
  ?extents: ('a -> int64 -> int64 -> flags -> extent list) ->
  ?cache: ('a -> int64 -> int64 -> flags -> unit) ->

  (* Miscellaneous. *)
  ?dump_plugin: (unit -> unit) ->
  ?list_exports: (bool -> bool -> export list) ->
  ?default_export: (bool -> bool -> string) ->

  unit ->
  unit
(** Register the plugin with nbdkit.

    The ['a] parameter is the handle type returned by your
    [~open_connection] method and passed back to all connected calls. *)

(** {2 Errors and debugging} *)

val set_error : Unix.error -> unit
(** Set the errno returned over the NBD protocol to the client.

    Note that the NBD protocol only supports the following
    errno values: [Unix.EROFS], [EPERM], [EIO], [ENOMEM], [ENOSPC],
    [ESHUTDOWN], [ENOTSUP], [EOVERFLOW] and [EINVAL].
    Any other errno will be translated to [EINVAL].

    @see <https://libguestfs.org/nbdkit_set_error.3.html> nbdkit_set_error(3) *)

val debug : ('a, unit, string, unit) format4 -> 'a
(** Print a debug message when nbdkit is in verbose mode.

    @see <https://libguestfs.org/nbdkit_debug.3.html> nbdkit_debug(3) *)

val debug_hexdump : buf -> string option -> int64 -> unit
(** Display buffer in canonical hexdump format, sending the result
    to debug output (when in verbose mode).

    @see <https://libguestfs.org/nbdkit_debug_hexdump.3.html> nbdkit_debug_hexdump(3) *)

val debug_hexdiff : buf -> buf -> string option -> int64 -> unit
(** Display differences in two buffers (of equal length).

    @see <https://libguestfs.org/nbdkit_debug_hexdiff.3.html> nbdkit_debug_hexdiff(3) *)

val timestamp : unit -> string
(** Generate a timestamp for log messages.

    @see <https://libguestfs.org/nbdkit_timestamp.3.html> nbdkit_timestamp(3) *)

(** {2 Parsing and configuration} *)

(* Note OCaml has functions already for parsing other integers, so
 * there is no need to bind them here.  We only bind the functions
 * which have special abilities in nbdkit: [parse_size] can parse
 * human sizes, [parse_probability], [parse_bool] and [parse_delay]
 * parse a range of nbdkit-specific strings, and [read_password]
 * suppresses echo.
 *)

val parse_size : string -> int64
(** Parse size parameter.

    @see <https://libguestfs.org/nbdkit_parse_size.3.html> nbdkit_parse_size(3)
    @raise Invalid_argument on error.  The actual error is sent to
    the nbdkit error log and is not available from the OCaml code.
    It is usually best to let the exception escape. *)

val parse_probability : string -> string -> float
(** Parse probability parameter.

    @see <https://libguestfs.org/nbdkit_parse_probability.3.html> nbdkit_parse_probability(3)
    @raise Invalid_argument on error. *)

val parse_bool : string -> bool
(** Parse boolean parameter.

    @see <https://libguestfs.org/nbdkit_parse_bool.3.html> nbdkit_parse_bool(3)
    @raise Invalid_argument on error. *)

val parse_delay : string -> string -> int * int
(** Parse delay parameter.

    @see <https://libguestfs.org/nbdkit_parse_delay.3.html> nbdkit_parse_delay(3)
    @raise Invalid_argument on error. *)

val read_password : string -> string
(** Read a password.

    @see <https://libguestfs.org/nbdkit_read_password.3.html> nbdkit_read_password(3)
    @raise Invalid_argument on error. *)

(* OCaml's [Filename] module can handle [absolute_path]. *)

val realpath : string -> string
(** @return the canonical path from a path parameter.

    @see <https://libguestfs.org/nbdkit_realpath.3.html> nbdkit_realpath(3) *)

val stdio_safe : unit -> bool
(** @return true if it is safe to interact with stdin and stdout
    during the configuration phase.

    @see <https://libguestfs.org/nbdkit_stdio_safe.3.html> nbdkit_stdio_safe(3) *)

(** {2 Shutdown and client disconnect} *)

val shutdown : unit -> unit
(** Requests the server shut down.

    @see <https://libguestfs.org/nbdkit_shutdown.3.html> nbdkit_shutdown(3) *)

val disconnect : bool -> unit
(** Requests disconnecting current client.

    @see <https://libguestfs.org/nbdkit_disconnect.3.html> nbdkit_disconnect(3) *)

(** {2 Client information} *)

val export_name : unit -> string
(** @return the name of the export as requested by the client.

    @see <https://libguestfs.org/nbdkit_export_name.3.html> nbdkit_export_name(3) *)

val is_tls : unit -> bool
(** @return true if the client completed TLS authentication.

    @see <https://libguestfs.org/nbdkit_is_tls.3.html> nbdkit_is_tls(3) *)

val peer_name : unit -> Unix.sockaddr
(** @return the socket address of the client.

    @see <https://libguestfs.org/nbdkit_peer_name.3.html> nbdkit_peer_name(3) *)

val peer_pid : unit -> int64
(** @return the process ID of the client.

    @see <https://libguestfs.org/nbdkit_peer_pid.3.html> nbdkit_peer_pid(3) *)

val peer_uid : unit -> int64
(** @return the user ID of the client.

    @see <https://libguestfs.org/nbdkit_peer_uid.3.html> nbdkit_peer_uid(3) *)

val peer_gid : unit -> int64
(** @return the group ID of the client.

    @see <https://libguestfs.org/nbdkit_peer_gid.3.html> nbdkit_peer_gid(3) *)

val peer_security_context : unit -> string
(** @return the security context or label of the client.

    @see <https://libguestfs.org/nbdkit_peer_security_context.3.html> nbdkit_peer_security_context(3) *)

val peer_tls_dn : unit -> string
val peer_tls_issuer_dn : unit -> string
(** @return the client TLS X.509 Distinguished Name

    @see <https://libguestfs.org/nbdkit_peer_tls_dn.3.html> nbdkit_peer_tls_dn(3)

    @see <https://libguestfs.org/nbdkit_peer_tls_issuer_dn.3.html> nbdkit_peer_tls_issuer_dn(3) *)

(** {2 Sleeping} *)

val nanosleep : int -> int -> unit
(** Sleeps for seconds and nanoseconds.

    @see <https://libguestfs.org/nbdkit_nanosleep.3.html> nbdkit_nanosleep(3) *)

(** {2 Version} *)

val version : unit -> string
(** @return the version of nbdkit that the plugin was compiled with. *)

val api_version : unit -> int
(** @return the nbdkit API version used by the plugin.

    @see <https://libguestfs.org/nbdkit-plugin.3.html>
    The description of [NBDKIT_API_VERSION] in nbdkit-plugin(3). *)

(** {2 Server information} *)

val name : unit -> string option
(** Get the name of the nbdkit instance.  This is the string
    which was set through the nbdkit [--name] parameter.
    If the name was not set, this returns [None].

    @see <https://libguestfs.org/nbdkit_name.3.html> nbdkit_name(3) *)
