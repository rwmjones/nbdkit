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

open Printf

exception Error of Unix.error option * string

type flags = flag list
and flag = May_trim | FUA | Req_one

type fua_flag = FuaNone | FuaEmulate | FuaNative

type cache_flag = CacheNone | CacheEmulate | CacheNop

type thread_model =
| THREAD_MODEL_SERIALIZE_CONNECTIONS
| THREAD_MODEL_SERIALIZE_ALL_REQUESTS
| THREAD_MODEL_SERIALIZE_REQUESTS
| THREAD_MODEL_PARALLEL

type buf =
  (char, Bigarray.int8_unsigned_elt, Bigarray.c_layout) Bigarray.Array1.t

let buf_len = Bigarray.Array1.dim

let buf_create len =
  Bigarray.Array1.create Bigarray.char Bigarray.c_layout len

let set_buf_to_zero buf =
  Bigarray.Array1.fill buf '\000'

let buf_zeroes len =
  let buf = buf_create len in
  set_buf_to_zero buf;
  buf

external _blit_from_string : string -> int -> buf -> int -> int -> unit
  = "ocaml_nbdkit_blit_from" [@@noalloc]
external _blit_from_bytes : bytes -> int -> buf -> int -> int -> unit
  = "ocaml_nbdkit_blit_from" [@@noalloc]
external _blit_to_bytes : buf -> int -> bytes -> int -> int -> unit
  = "ocaml_nbdkit_blit_to_bytes" [@@noalloc]

let blit_string_to_buf src src_pos buf buf_pos len =
  if len < 0 || src_pos < 0 || src_pos > String.length src - len
             || buf_pos < 0 || buf_pos > buf_len buf - len
  then invalid_arg "NBDKit.blit_string_to_buf";
  _blit_from_string src src_pos buf buf_pos len

let blit_bytes_to_buf src src_pos buf buf_pos len =
  if len < 0 || src_pos < 0 || src_pos > Bytes.length src - len
             || buf_pos < 0 || buf_pos > buf_len buf - len
  then invalid_arg "NBDKit.blit_bytes_to_buf";
  _blit_from_bytes src src_pos buf buf_pos len

let blit_buf_to_bytes buf buf_pos dst dst_pos len =
  if len < 0 || buf_pos < 0 || buf_pos > buf_len buf - len
             || dst_pos < 0 || dst_pos > Bytes.length dst - len
  then invalid_arg "NBDKit.blit_buf_to_bytes";
  _blit_to_bytes buf buf_pos dst dst_pos len

let buf_of_string s =
  let len = String.length s in
  let buf = buf_create len in
  blit_string_to_buf s 0 buf 0 len;
  buf

let buf_of_bytes b =
  let len = Bytes.length b in
  let buf = buf_create len in
  blit_bytes_to_buf b 0 buf 0 len;
  buf

let bytes_of_buf buf =
  let len = buf_len buf in
  let b = Bytes.create len in
  blit_buf_to_bytes buf 0 b 0 len;
  b

let string_of_buf buf = bytes_of_buf buf |> Bytes.to_string

type extent = {
  offset : int64;
  length : int64;
  is_hole : bool;
  is_zero : bool;
}

type export = {
  name : string;
  description : string option;
}

(* Set a named string field in the C plugin struct. *)
external set_string_field : string -> string -> unit
  = "ocaml_nbdkit_set_string_field" [@@noalloc]

(* Set an arbitrary named function pointer field in the C plugin struct.
 *
 * Caution: There is no type checking here, the parameter type
 * declared in [NBDKit.mli] must match what the corresponding
 * [<field_name>_wrapper] function in [plugin.c] calls.
 *)
external set_field : string -> 'a -> unit = "ocaml_nbdkit_set_field"

(* Register the plugin. *)
let register_plugin ~name
                    ?longname
                    ?version
                    ?description
                    ?load
                    ?get_ready
                    ?after_fork
                    ?cleanup
                    ?unload
                    ?config
                    ?config_complete
                    ?config_help
                    ?thread_model
                    ?magic_config_key
                    ?preconnect
                    ~open_connection
                    ?close
                    ~get_size
                    ?export_description
                    ?block_size
                    ?can_cache
                    ?can_extents
                    ?can_fast_zero
                    ?can_flush
                    ?can_fua
                    ?can_multi_conn
                    ?can_trim
                    ?can_write
                    ?can_zero
                    ?is_rotational
                    ~pread
                    ?pwrite
                    ?flush
                    ?trim
                    ?zero
                    ?extents
                    ?cache
                    ?dump_plugin
                    ?list_exports
                    ?default_export
                    () =
  (* Set fields in the C plugin struct. *)
  set_string_field "name" name;
  set_field "open" open_connection;
  set_field "pread" pread;
  set_field "get_size" get_size;

  let may f = function None -> () | Some a -> f a in
  may (set_string_field "longname") longname;
  may (set_string_field "version") version;
  may (set_string_field "description") description;
  may (set_string_field "config_help") config_help;
  may (set_string_field "magic_config_key") magic_config_key;

  may (set_field "after_fork") after_fork;
  may (set_field "block_size") block_size;
  may (set_field "cache") cache;
  may (set_field "can_cache") can_cache;
  may (set_field "can_extents") can_extents;
  may (set_field "can_fast_zero") can_fast_zero;
  may (set_field "can_flush") can_flush;
  may (set_field "can_fua") can_fua;
  may (set_field "can_multi_conn") can_multi_conn;
  may (set_field "can_trim") can_trim;
  may (set_field "can_write") can_write;
  may (set_field "can_zero") can_zero;
  may (set_field "cleanup") cleanup;
  may (set_field "close") close;
  may (set_field "config") config;
  may (set_field "config_complete") config_complete;
  may (set_field "default_export") default_export;
  may (set_field "dump_plugin") dump_plugin;
  may (set_field "export_description") export_description;
  may (set_field "extents") extents;
  may (set_field "flush") flush;
  may (set_field "get_ready") get_ready;
  may (set_field "is_rotational") is_rotational;
  may (set_field "list_exports") list_exports;
  may (set_field "load") load;
  may (set_field "preconnect") preconnect;
  may (set_field "pwrite") pwrite;
  may (set_field "thread_model") thread_model;
  may (set_field "trim") trim;
  may (set_field "unload") unload;
  may (set_field "zero") zero

(* Bindings to nbdkit server functions. *)
external api_version : unit -> int = "ocaml_nbdkit_api_version"
external _debug : string -> unit = "ocaml_nbdkit_debug" [@@noalloc]
let debug fs = ksprintf _debug fs
external debug_hexdump : buf -> string option -> int64 -> unit =
  "ocaml_nbdkit_debug_hexdump"
external debug_hexdiff : buf -> buf -> string option -> int64 -> unit =
  "ocaml_nbdkit_debug_hexdiff"
external disconnect : bool -> unit = "ocaml_nbdkit_disconnect" [@@noalloc]
external export_name : unit -> string = "ocaml_nbdkit_export_name"
external is_tls : unit -> bool = "ocaml_nbdkit_is_tls"
external name : unit -> string option = "ocaml_nbdkit_name"
external nanosleep : int -> int -> unit = "ocaml_nbdkit_nanosleep"
external parse_bool : string -> bool = "ocaml_nbdkit_parse_bool"
external parse_delay : string -> string -> int * int =
  "ocaml_nbdkit_parse_delay"
external parse_probability : string -> string -> float =
  "ocaml_nbdkit_parse_probability"
external parse_size : string -> int64 = "ocaml_nbdkit_parse_size"
external peer_gid : unit -> int64 = "ocaml_nbdkit_peer_gid"
external peer_name : unit -> Unix.sockaddr = "ocaml_nbdkit_peer_name"
external peer_pid : unit -> int64 = "ocaml_nbdkit_peer_pid"
external peer_security_context : unit -> string =
  "ocaml_nbdkit_peer_security_context"
external peer_tls_dn : unit -> string = "ocaml_nbdkit_peer_tls_dn"
external peer_tls_issuer_dn : unit -> string =
  "ocaml_nbdkit_peer_tls_issuer_dn"
external peer_uid : unit -> int64 = "ocaml_nbdkit_peer_uid"
external read_password : string -> string = "ocaml_nbdkit_read_password"
external realpath : string -> string = "ocaml_nbdkit_realpath"
external set_error : Unix.error -> unit = "ocaml_nbdkit_set_error" [@@noalloc]
external shutdown : unit -> unit = "ocaml_nbdkit_shutdown" [@@noalloc]
external stdio_safe : unit -> bool = "ocaml_nbdkit_stdio_safe"
external timestamp : unit -> string = "ocaml_nbdkit_timestamp"
external version : unit -> string = "ocaml_nbdkit_version"
