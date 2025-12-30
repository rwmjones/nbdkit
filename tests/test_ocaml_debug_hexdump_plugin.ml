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

open Printf

(* Print something during module initialization, useful for debugging
 * obscure OCaml startup issues.
 *)
let () =
  eprintf "test_ocaml_debug_hexdump_plugin.ml: module initializing\n%!"

let load () =
  NBDKit.debug "test ocaml debug hexdump plugin loaded"

let unload () =
  (* A good way to find memory bugs: *)
  Gc.compact ();
  NBDKit.debug "test ocaml debug hexdump plugin unloaded"

let rec open_connection readonly = ()

let get_size () = 1000000_L

let pread () buf offset _ =
  NBDKit.set_buf_to_zero buf

let pwrite () buf offset _ =
  NBDKit.debug_hexdump buf (Some "CAMLDUMP: ") offset

let thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_ALL_REQUESTS

let () =
  NBDKit.register_plugin
    ~name:   "hexdump"
    ~version: (NBDKit.version ())

    ~load
    ~unload

    ~open_connection
    ~get_size
    ~pread
    ~pwrite
    ()
