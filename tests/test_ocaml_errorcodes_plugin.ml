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

(* This plugin is used to test returning error codes from OCaml
 * plugins.  Depending on the sector requested, it returns a different
 * error code (except for sector 0 where it returns data).
 *)

open Unix

let sector_size = 512_L

(* This must match the table in test-ocaml-errorcodes.c *)
let sectors = [|
  (* 0 *) None (* no error *);
  (* 1 *) Some (EPERM, "EPERM");
  (* 2 *) Some (EIO, "EIO");
  (* 3 *) Some (ENOMEM, "ENOMEM");
  (* 4 *) Some (ESHUTDOWN, "ESHUTDOWN");
  (* 5 *) Some (EINVAL, "EINVAL");
  (* 6 *) None (* no error *);
|]

let name = "test-ocaml-errorcodes"

let open_connection _ = ()

let get_size () = Int64.mul (Array.length sectors |> Int64.of_int) sector_size

let pread () buf offset _ =
  let sector = Int64.div offset sector_size |> Int64.to_int in
  match sectors.(sector) with
  | None -> Bigarray.Array1.fill buf '\000'
  | Some (err, str) -> NBDKit.set_error err; failwith str

let unload () =
  (* A good way to find memory bugs: *)
  Gc.compact ();
  NBDKit.debug "%s plugin unloaded" name

let () =
  NBDKit.register_plugin
    ~name
    ~version: (NBDKit.version ())
    ~unload

    ~open_connection
    ~get_size
    ~pread
    ()
