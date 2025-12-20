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
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <caml/alloc.h>
#include <caml/bigarray.h>
#include <caml/callback.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>
#include <caml/printexc.h>
#include <caml/threads.h>
#include <caml/unixsupport.h>
#include <caml/version.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "array-size.h"

#include "plugin.h"

/* OCaml >= 5 requires that each thread except the main thread is
 * registered with the OCaml runtime, and unregistered before the
 * thread exits.  nbdkit doesn't provide APIs to notify us when a new
 * thread is created or destroyed, instead it just calls random entry
 * points on new threads.  So we have to register threads with the
 * OCaml runtime ourselves.
 *
 * We maintain a thread-local flag which tracks if we have registered
 * the thread.
 *
 * It also tracks if this thread is the main thread (ie. the thread
 * which called caml_startup) because that thread must not be
 * registered.  This scheme was suggested by Guillaume Munch-
 * Maccagnoni:
 * https://github.com/ocaml/ocaml/issues/13400#issuecomment-2309956292.
 *
 * For OCaml < 5 only, you shouldn't link the plugin with threads.cmxa
 * since that breaks nbdkit forking.  Symbols caml_c_thread_register
 * and caml_c_thread_unregister are pulled in only when you link with
 * threads.cmxa (which pulls in ocamllib/libthreads.a as a
 * side-effect), so when _not_ using threads.cmxa these symbols are
 * not present.
 */
#if OCAML_VERSION_MAJOR < 5

#define init_threads() /* nothing */
#define register_thread() /* nothing */
#define unregister_thread() /* nothing */

#else /* OCAML_VERSION_MAJOR >= 5 */

/* The key can have the following values:
 * NULL = new thread, not registered
 * &thread_key_main = this is the main thread, do not register
 * &thread_key_non_main = this is a non-main thread that has been registered
 */
static pthread_key_t thread_key;
/* These globals are just used as addresses: */
static char thread_key_main;
static char thread_key_non_main;

static void destroy_thread (void *val);

static void
init_threads (void)
{
  pthread_key_create (&thread_key, destroy_thread);

  /* Mark this as the main thread. */
  pthread_setspecific (thread_key, &thread_key_main);
}

static void
register_thread (void)
{
  void *val = pthread_getspecific (thread_key);
  if (val == NULL) {
    /* Register this non-main thread, and remember that we did it. */
    if (caml_c_thread_register () == 0) abort ();
    pthread_setspecific (thread_key, &thread_key_non_main);
  }
}

static void
destroy_thread (void *val)
{
  if (val == &thread_key_non_main) {
    /* Unregister this non-main thread.
     *
     * Originally we called abort() on failure here, but that causes
     * problems under valgrind (only).  Since unregistering the thread
     * is essentially optional, and the failure isn't actionable,
     * don't worry about it.
     */
    /*if (caml_c_thread_unregister () == 0) abort ();*/
    caml_c_thread_unregister ();
    pthread_setspecific (thread_key, NULL);
  }
}

#endif /* OCAML_VERSION_MAJOR >= 5 */

/* Instead of using the NBDKIT_REGISTER_PLUGIN macro, we construct the
 * nbdkit_plugin struct and return it from our own plugin_init
 * function.
 */
static int after_fork_wrapper (void);
static void close_wrapper (void *h);
static void unload_wrapper (void);
static void free_strings (void);
static void remove_roots (void);

static pid_t original_pid;

static struct nbdkit_plugin plugin = {
  ._struct_size = sizeof (plugin),
  ._api_version = NBDKIT_API_VERSION,
  ._thread_model = NBDKIT_THREAD_MODEL_PARALLEL,

  /* The following field is used as a canary to detect whether the
   * OCaml code started up and called us back successfully.  If it's
   * still set to NULL (see plugin_init below), then we can print a
   * suitable error message.
   */
  .name = NULL,

  /* We always call these, even if the OCaml code does not provide a
   * callback.
   */
  .after_fork = after_fork_wrapper,
  .close = close_wrapper,
  .unload = unload_wrapper,
};

NBDKIT_DLL_PUBLIC struct nbdkit_plugin *
plugin_init (void)
{
  char *argv[2] = { "nbdkit", NULL };

  /* Initialize OCaml runtime. */
  caml_startup (argv);

  /* We need to release the runtime system here so other threads may
   * use it.  Before we call any OCaml callbacks we must acquire the
   * runtime system again.
   */
  do_caml_release_runtime_system ();

  /* Initialize thread-specific key. */
  init_threads ();

  /* Save the PID so we know in after_fork if we forked. */
  original_pid = getpid ();

  /* It is expected that top level statements in the OCaml code have
   * by this point called NBDKit.register_plugin.  We know if this was
   * called because plugin.name will have been set (by
   * set_string_field "name").  If that didn't happen, something went
   * wrong so exit here.
   */
  if (plugin.name == NULL) {
    fprintf (stderr, "error: OCaml code did not call NBDKit.register_plugin\n");
    exit (EXIT_FAILURE);
  }
  return &plugin;
}

/* There is one global per callback called <callback>_fn.  These
 * globals store the OCaml functions that we actually call.  Also the
 * assigned ones are roots to ensure the GC doesn't free them.
 */
#define CB(name) static value name##_fn;
#include "callbacks.h"
#undef CB

/*----------------------------------------------------------------------*/
/* Wrapper functions that translate calls from C (ie. nbdkit) to OCaml. */

static bool
exception_to_error (const char *func, value rv)
{
  CAMLparam0 ();
  CAMLlocal2 (exn, v);
  const bool is_exn = Is_exception_result (rv);
  const char *exn_name;

  if (is_exn) {
    /* See libguestfs.git daemon/daemon-c.c */
    exn = Extract_exception (rv);

    if (Tag_val (Field (exn, 0)) == String_tag)
      exn_name = String_val (Field (exn, 0));
    else
      exn_name = String_val (Field (Field (exn, 0), 0));

    nbdkit_debug ("ocaml: '%s' raised exception '%s'", func, exn_name);

    if (strcmp (exn_name, "NBDKit.Error") == 0) {
      v = Field (exn, 1);
      if (v != Val_int (0)) /* Some error */
        nbdkit_set_error (code_of_unix_error (Field (v, 0)));
      nbdkit_error ("%s", String_val (Field (exn, 2)));
    }
    else if (strcmp (exn_name, "Failure") == 0) {
      nbdkit_error ("%s", String_val (Field (exn, 1)));
    }
    else if (strcmp (exn_name, "Invalid_argument") == 0) {
      nbdkit_error ("invalid argument: %s", String_val (Field (exn, 1)));
    }
    else if (strcmp (exn_name, "Unix.Unix_error") == 0) {
      nbdkit_set_error (code_of_unix_error (Field (exn, 1)));
      nbdkit_error ("%s: %s: %s", func,
                    String_val (Field (exn, 2)),
                    String_val (Field (exn, 3)));
    }
    else {
      /* General exception. */
      nbdkit_error ("%s: %s", func, caml_format_exception (exn));
    }
  }

  CAMLreturnT (bool, is_exn);
}

/* This macro calls nbdkit_error when we get an exception thrown in
 * OCaml callback code.  The 'return_stmt' parameter is usually a call
 * to CAMLreturnT, but may be empty in order to fall through.
 */
#define EXCEPTION_TO_ERROR(rv, return_stmt)                             \
  do {                                                                  \
    if (exception_to_error (__func__, (rv)))                            \
      return_stmt;                                                      \
  } while (0)

static void
load_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  caml_callback (load_fn, Val_unit);
}

/* We always have an unload function, since it also has to free the
 * globals we allocated.
 */
static void
unload_wrapper (void)
{
  register_thread ();
  /* NB: Don't call ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE here since it
   * will release the lock after caml_shutdown (which is a bug), and
   * the runtime system must be locked when calling caml_shutdown.
   */
  do_caml_acquire_runtime_system ();

  if (unload_fn) {
    caml_callback (unload_fn, Val_unit);
  }

  free_strings ();
  remove_roots ();

#ifdef HAVE_CAML_SHUTDOWN
  caml_shutdown ();
#endif
}

static void
dump_plugin_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  printf ("ocaml_version_major=%d\n", OCAML_VERSION_MAJOR);
  printf ("ocaml_version_minor=%d\n", OCAML_VERSION_MINOR);
  printf ("ocaml_version=%s\n", OCAML_VERSION_STRING);

  rv = caml_callback_exn (dump_plugin_fn, Val_unit);
  EXCEPTION_TO_ERROR (rv, /* fallthrough */);
  CAMLreturn0;
}

static int
config_wrapper (const char *key, const char *val)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal3 (keyv, valv, rv);

  keyv = caml_copy_string (key);
  valv = caml_copy_string (val);

  rv = caml_callback2_exn (config_fn, keyv, valv);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
config_complete_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  rv = caml_callback_exn (config_complete_fn, Val_unit);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
thread_model_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  rv = caml_callback_exn (thread_model_fn, Val_unit);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Int_val (rv));
}

static int
get_ready_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  rv = caml_callback_exn (get_ready_fn, Val_unit);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

/* We always have an after_fork wrapper, since if we really forked
 * then we must reinitialize the OCaml runtime.
 */
static int
after_fork_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

#if OCAML_VERSION_MAJOR >= 5
  /* If we forked, OCaml 5 requires that we reinitialize the runtime. */
  if (getpid () != original_pid) {
    extern void (*caml_atfork_hook)(void);
    caml_atfork_hook ();
  }
#endif

  if (after_fork_fn) {
    rv = caml_callback_exn (after_fork_fn, Val_unit);
    EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));
  }

  CAMLreturnT (int, 0);
}

static void
cleanup_wrapper (void)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  rv = caml_callback_exn (cleanup_fn, Val_unit);
  EXCEPTION_TO_ERROR (rv, CAMLreturn0);

  CAMLreturn0;
}

static int
preconnect_wrapper (int readonly)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);

  rv = caml_callback_exn (preconnect_fn, Val_bool (readonly));
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
list_exports_wrapper (int readonly, int is_tls, struct nbdkit_exports *exports)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal2 (rv, v);

  rv = caml_callback2_exn (list_exports_fn, Val_bool (readonly),
                           Val_bool (is_tls));
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  /* Convert exports list into calls to nbdkit_add_export. */
  while (rv != Val_emptylist) {
    const char *name, *desc = NULL;

    v = Field (rv, 0);          /* export struct */
    name = String_val (Field (v, 0));
    if (Is_block (Field (v, 1)))
      desc = String_val (Field (Field (v, 1), 0));
    if (nbdkit_add_export (exports, name, desc) == -1) {
      CAMLreturnT (int, -1);
    }

    rv = Field (rv, 1);
  }

  CAMLreturnT (int, 0);
}

static const char *
default_export_wrapper (int readonly, int is_tls)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  const char *name;

  rv = caml_callback2_exn (default_export_fn, Val_bool (readonly),
                           Val_bool (is_tls));
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (const char *, NULL));

  name = nbdkit_strdup_intern (String_val (rv));
  CAMLreturnT (const char *, name);
}

/* The C handle. */
struct handle {
  value v;       /* The OCaml handle, also registered as a GC root. */
};

static void *
open_wrapper (int readonly)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h;

  rv = caml_callback_exn (open_fn, Val_bool (readonly));
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (void *, NULL));

  /* Allocate a root on the C heap that points to the OCaml handle. */
  h = malloc (sizeof *h);
  if (h == NULL) {
    nbdkit_error ("malloc: %m");
    CAMLreturnT (void *, NULL);
  }
  h->v = rv;
  caml_register_generational_global_root (&h->v);

  CAMLreturnT (void *, h);
}

/* We always have a close wrapper, since we need to unregister the
 * global root and free the handle.
 */
static void
close_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  if (close_fn) {
    rv = caml_callback_exn (close_fn, h->v);
    EXCEPTION_TO_ERROR (rv, /* fallthrough */);
  }

  caml_remove_generational_global_root (&h->v);
  free (h);

  CAMLreturn0;
}

static const char *
export_description_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;
  const char *desc;

  rv = caml_callback_exn (export_description_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (const char *, NULL));

  desc = nbdkit_strdup_intern (String_val (rv));
  CAMLreturnT (const char *, desc);
}

static int64_t
get_size_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;
  int64_t r;

  rv = caml_callback_exn (get_size_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int64_t, -1));

  r = Int64_val (rv);
  CAMLreturnT (int64_t, r);
}

static int
block_size_wrapper (void *hv,
                    uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;
  int i;
  int64_t i64;

  rv = caml_callback_exn (block_size_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  i = Int_val (Field  (rv, 0));
  if (i < 0 || i > 65536) {
    nbdkit_error ("minimum block size must be in range 1..65536");
    CAMLreturnT (int, -1);
  }
  *minimum = i;

  i = Int_val (Field  (rv, 1));
  if (i < 512 || i > 32 * 1024 * 1024) {
    nbdkit_error ("preferred block size must be in range 512..32M");
    CAMLreturnT (int, -1);
  }
  *preferred = i;

  i64 = Int64_val (Field  (rv, 2));
  if (i64 < -1 || i64 > UINT32_MAX) {
    nbdkit_error ("maximum block size out of range");
    CAMLreturnT (int, -1);
  }
  if (i64 == -1) /* Allow -1L to mean greatest block size. */
    *maximum = (uint32_t)-1;
  else
    *maximum = i;

  CAMLreturnT (int, 0);
}

static int
can_write_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_write_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_flush_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_flush_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
is_rotational_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (is_rotational_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_trim_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_trim_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_zero_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_zero_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_fua_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_fua_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Int_val (rv));
}

static int
can_fast_zero_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_fast_zero_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_cache_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_cache_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Int_val (rv));
}

static int
can_extents_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_extents_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static int
can_multi_conn_wrapper (void *hv)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal1 (rv);
  struct handle *h = hv;

  rv = caml_callback_exn (can_multi_conn_fn, h->v);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, Bool_val (rv));
}

static value
Val_flags (uint32_t flags)
{
  CAMLparam0 ();
  CAMLlocal2 (consv, rv);

  rv = Val_unit;
  if (flags & NBDKIT_FLAG_MAY_TRIM) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 0); /* 0 = May_trim */
    Store_field (consv, 1, rv);
    rv = consv;
  }
  if (flags & NBDKIT_FLAG_FUA) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 1); /* 1 = FUA */
    Store_field (consv, 1, rv);
    rv = consv;
  }
  if (flags & NBDKIT_FLAG_REQ_ONE) {
    consv = caml_alloc (2, 0);
    Store_field (consv, 0, 2); /* 2 = Req_one */
    Store_field (consv, 1, rv);
    rv = consv;
  }

  CAMLreturn (rv);
}

/* Wrap the buf in an OCaml bigarray so that callers can read and
 * write directly to it without copying.
 * https://discuss.ocaml.org/t/is-there-a-simple-library-that-wraps-a-c-mallocd-char-buffer/14323
 * https://v2.ocaml.org/manual/intfc.html#ss:C-Bigarrays-wrap
 */
static int
pread_wrapper (void *hv, void *buf, uint32_t count, uint64_t offset,
               uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal4 (rv, ba, offsetv, flagsv);
  struct handle *h = hv;
  long dims[1] = { count };

  ba = caml_ba_alloc (CAML_BA_CHAR|CAML_BA_C_LAYOUT, 1, buf, dims);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, ba, offsetv, flagsv };
  rv = caml_callbackN_exn (pread_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
pwrite_wrapper (void *hv, const void *buf, uint32_t count, uint64_t offset,
                uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal4 (rv, ba, offsetv, flagsv);
  struct handle *h = hv;
  long dims[1] = { count };

  /* We discard the const of the incoming buffer, and in theory OCaml
   * plugins could try writing to it. XXX
   */
  ba = caml_ba_alloc (CAML_BA_CHAR|CAML_BA_C_LAYOUT, 1, (void *) buf, dims);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, ba, offsetv, flagsv };
  rv = caml_callbackN_exn (pwrite_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
flush_wrapper (void *hv, uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal2 (rv, flagsv);
  struct handle *h = hv;

  flagsv = Val_flags (flags);

  rv = caml_callback2_exn (flush_fn, h->v, flagsv);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
trim_wrapper (void *hv, uint32_t count, uint64_t offset, uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  struct handle *h = hv;

  countv = caml_copy_int64 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (trim_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
zero_wrapper (void *hv, uint32_t count, uint64_t offset, uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  struct handle *h = hv;

  countv = caml_copy_int64 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (zero_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

static int
extents_wrapper (void *hv, uint32_t count, uint64_t offset, uint32_t flags,
                 struct nbdkit_extents *extents)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal5 (rv, countv, offsetv, flagsv, v);
  struct handle *h = hv;

  countv = caml_copy_int64 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (extents_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  /* Convert extents list into calls to nbdkit_add_extent. */
  while (rv != Val_emptylist) {
    uint64_t length;
    uint32_t type = 0;

    v = Field (rv, 0);          /* extent struct */
    offset = Int64_val (Field (v, 0));
    length = Int64_val (Field (v, 1));
    if (Bool_val (Field (v, 2)))
      type |= NBDKIT_EXTENT_HOLE;
    if (Bool_val (Field (v, 3)))
      type |= NBDKIT_EXTENT_ZERO;
    if (nbdkit_add_extent (extents, offset, length, type) == -1) {
      CAMLreturnT (int, -1);
    }

    rv = Field (rv, 1);
  }

  CAMLreturnT (int, 0);
}

static int
cache_wrapper (void *hv, uint32_t count, uint64_t offset, uint32_t flags)
{
  register_thread ();
  ACQUIRE_RUNTIME_FOR_CURRENT_SCOPE ();
  CAMLparam0 ();
  CAMLlocal4 (rv, countv, offsetv, flagsv);
  struct handle *h = hv;

  countv = caml_copy_int64 (count);
  offsetv = caml_copy_int64 (offset);
  flagsv = Val_flags (flags);

  value args[] = { h->v, countv, offsetv, flagsv };
  rv = caml_callbackN_exn (cache_fn, ARRAY_SIZE (args), args);
  EXCEPTION_TO_ERROR (rv, CAMLreturnT (int, -1));

  CAMLreturnT (int, 0);
}

/*----------------------------------------------------------------------*/
/* set_* functions called from OCaml code at load time to initialize
 * fields in the plugin struct.
 */

/* NB: noalloc function */
NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_string_field (value fieldv, value strv)
{
  const char *field = String_val (fieldv);
  char *str = strdup (String_val (strv));

  if (strcmp (field, "name") == 0)
    plugin.name = str;
  else if (strcmp (field, "longname") == 0)
    plugin.longname = str;
  else if (strcmp (field, "version") == 0)
    plugin.version = str;
  else if (strcmp (field, "description") == 0)
    plugin.description = str;
  else if (strcmp (field, "config_help") == 0)
    plugin.config_help = str;
  else if (strcmp (field, "magic_config_key") == 0)
    plugin.magic_config_key = str;
  else
    abort ();                   /* unknown field name */

  return Val_unit;
}

/* Free string fields, called from unload(). */
static void
free_strings (void)
{
  free ((char *) plugin.name);
  free ((char *) plugin.longname);
  free ((char *) plugin.version);
  free ((char *) plugin.description);
  free ((char *) plugin.config_help);
  free ((char *) plugin.magic_config_key);
}

NBDKIT_DLL_PUBLIC value
ocaml_nbdkit_set_field (value fieldv, value fv)
{
  CAMLparam2 (fieldv, fv);

  /* This isn't very efficient because we string-compare the field
   * names.  However it is only called when the plugin is being loaded
   * for a handful of fields so it's not performance critical.
   */
#define CB(name)                                         \
  if (strcmp (String_val (fieldv), #name) == 0) {        \
    plugin.name = name##_wrapper;                        \
    assert (!name##_fn);                                 \
    name##_fn = fv;                                      \
    caml_register_generational_global_root (&name##_fn); \
  } else
#include "callbacks.h"
#undef CB
  /* else if the field is not known */ abort ();

  CAMLreturn (Val_unit);
}

/* Called from unload() to remove the GC roots registered by set* functions. */
static void
remove_roots (void)
{
#define CB(name) \
  if (name##_fn) caml_remove_generational_global_root (&name##_fn);
#include "callbacks.h"
#undef CB
}
