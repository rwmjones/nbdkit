(* nbdkit
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

(* Print something during module initialization, useful for debugging
 * obscure OCaml startup issues.
 *)
let () = Printf.eprintf "test_ocaml_plugin.ml: module initializing\n%!"

let sector_size = 512
let nr_sectors = 2048

let disk = Bytes.make (nr_sectors*sector_size) '\000' (* disk image *)
let sparse = Bytes.make nr_sectors '\000' (* sparseness bitmap *)

(* Test parse_* functions. *)
let () =
  assert (NBDKit.parse_size "1M" = Int64.of_int (1024*1024));
  assert (NBDKit.parse_probability "test parse probability" "1:10" = 0.1);
  assert (NBDKit.parse_bool "true" = true);
  assert (NBDKit.parse_bool "0" = false)

(* Test the realpath function. *)
let () =
  let isdir d = try Sys.is_directory d with Sys_error _ -> false in
  let test_dir = "/usr/bin" in
  if isdir test_dir then
    (* We don't know what the answer will be, but it must surely
     * be a directory.
     *)
    assert (isdir (NBDKit.realpath test_dir))

(* Test [NBDKit.version ()] returns a sensible looking string. *)
let () =
  let ver = NBDKit.version () in
  assert (String.length ver > 2);
  assert (String.sub ver 0 2 = "1.")

(* Test [NBDKit.api_version ()]. *)
let () =
  assert (NBDKit.api_version () = 2)

(* Test [NBDKit.stdio_safe]. *)
let () =
  assert (NBDKit.stdio_safe ())

(* Test [NBDKit.debug_hexdiff]. *)
let () =
  let buf1 = NBDKit.buf_of_string "0123456789abcdef" in
  let buf2 = NBDKit.buf_of_string "0123456789XXcdef" in
  NBDKit.debug_hexdiff buf1 buf2 None 4L

(* Test [NBDKit.name]. *)
let () =
  NBDKit.debug "name = %s"
    (match NBDKit.name () with
     | None -> "None"
     | Some n -> n)

let load () =
  NBDKit.debug "test ocaml plugin loaded"

let unload () =
  (* A good way to find memory bugs: *)
  Gc.compact ();
  NBDKit.debug "test ocaml plugin unloaded"

(* See test-ocaml-dump-plugin.sh and test-ocaml-list-exports.sh *)
let dump_plugin () =
  Printf.printf "testocaml=42\n";
  Printf.printf "ocaml=%s\n" Sys.ocaml_version;
  flush stdout

let params = ref []

let config k v =
  params := (k, v) :: !params

let config_complete () =
  let params = List.rev !params in
  assert (params = [ "a", "1"; "b", "2"; "c", "3"; "d", "4" ])

let get_ready () =
  (* We could allocate the disk here, but it's easier to allocate
   * it statically above.
   *)
  NBDKit.debug "test ocaml plugin getting ready"

let after_fork () =
  NBDKit.debug "test ocaml plugin after fork"

let cleanup () =
  NBDKit.debug "test ocaml plugin cleaning up"

(* Test the handle is received by callbacks. *)
type handle = {
  h_id : int;
  h_sentinel : string;
}

let id = ref 0
let rec open_connection readonly =
  let export_name = NBDKit.export_name () and tls = NBDKit.is_tls () in
  NBDKit.debug "%s: test ocaml plugin handle opened \
                readonly=%b export=%S tls=%b"
    (NBDKit.timestamp ())
    readonly export_name tls;
  check_peer ();
  incr id;
  { h_id = !id; h_sentinel = "TESTING" }

(* This tests the nbdkit_peer_name function. *)
and check_peer () =
  try
    (match NBDKit.peer_name () with
     (* The test framework always uses a Unix domain socket.
      *
      * For some reason on Linux this always returns the path as "".
      * I checked the underlying getpeername system call and that
      * is exactly what is returned.
      *)
     | Unix.ADDR_UNIX sock ->
        NBDKit.debug "nbdkit_peer_name returned ADDR_UNIX %S" sock
     | _ ->
        failwith "nbdkit_peer_name returned an unexpected socket type"
    )
  with
    Failure msg when
         msg = "nbdkit_peer_name is not supported by this version of OCaml" ->
    NBDKit.debug "test skipped: %s" msg

let close h =
  NBDKit.debug "test ocaml plugin closing handle id=%d" h.h_id;
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  ()

(* See test-ocaml-list-exports.sh *)
let list_exports _ _ =
  [ { NBDKit.name = "name1"; description = Some "desc1" };
    { name = "name2"; description = None } ]

let default_export _ _ = "name1"

let get_size h =
  NBDKit.debug "test ocaml plugin get_size handle id=%d" h.h_id;
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  Int64.of_int (Bytes.length disk)

let block_size _ = (1, 4096, -1L)

let pread h buf offset _ =
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  let len = NBDKit.buf_len buf in
  let offset = Int64.to_int offset in
  NBDKit.blit_bytes_to_buf disk offset buf 0 len

let set_non_sparse offset len =
  Bytes.fill sparse (offset/sector_size) ((len-1)/sector_size) '\001'

let pwrite h buf offset _ =
  assert (h.h_id > 0);
  assert (h.h_sentinel = "TESTING");
  let len = NBDKit.buf_len buf in
  let offset = Int64.to_int offset in
  NBDKit.blit_buf_to_bytes buf 0 disk offset len;
  set_non_sparse offset len

let extents _ count offset _ =
  let extents = Array.init nr_sectors (
    fun sector ->
      { NBDKit.offset = Int64.of_int (sector*sector_size);
        length = Int64.of_int sector_size;
        is_hole = true; is_zero = false }
  ) in
  Bytes.iteri (
    fun i c ->
      if c = '\001' then (* not sparse *)
        extents.(i) <- { extents.(i) with is_hole = false }
  ) sparse;
  Array.to_list extents

let thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_ALL_REQUESTS

let () =
  NBDKit.register_plugin
    ~name:   "testocaml"
    ~version: (NBDKit.version ())

    ~load
    ~get_ready
    ~after_fork
    ~cleanup
    ~unload

    ~config
    ~config_complete
    ~thread_model
    ~magic_config_key: "d"

    ~open_connection
    ~close
    ~get_size
    ~block_size
    ~pread
    ~pwrite
    ~extents

    ~list_exports
    ~default_export

    ~dump_plugin
    ()
