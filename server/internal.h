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

#ifndef NBDKIT_INTERNAL_H
#define NBDKIT_INTERNAL_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <pthread.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef ENABLE_PROBES
#include <sys/sdt.h>
#else /* !ENABLE_PROBES */
/* If probes are disabled, allow the macros to appear but they don't
 * expand to anything.
 */
#define DTRACE_PROBE(pv,pb)
#define DTRACE_PROBE1(pv,pb,a1)
#define DTRACE_PROBE2(pv,pb,a1,a2)
#define DTRACE_PROBE3(pv,pb,a1,a2,a3)
#define DTRACE_PROBE4(pv,pb,a1,a2,a3,a4)
#define DTRACE_PROBE5(pv,pb,a1,a2,a3,a4,a5)
#define DTRACE_PROBE6(pv,pb,a1,a2,a3,a4,a5,a6)
#define DTRACE_PROBE7(pv,pb,a1,a2,a3,a4,a5,a6,a7)
#define DTRACE_PROBE8(pv,pb,a1,a2,a3,a4,a5,a6,a7,a8)
#define DTRACE_PROBE9(pv,pb,a1,a2,a3,a4,a5,a6,a7,a8,a9)
#define DTRACE_PROBE10(pv,pb,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10)
#define DTRACE_PROBE11(pv,pb,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11)
#define DTRACE_PROBE12(pv,pb,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12)
#endif /* !ENABLE_PROBES */

/* Do we have the --timeout option?
 *
 * OpenBSD defines timer_create as a function that returns -ENOSYS,
 * which breaks configure.  So we have to check for SIGEV_THREAD being
 * defined as well.
 * https://github.com/openbsd/src/blob/master/lib/libc/sys/timer_create.c
 */
#include <signal.h>
#include <time.h>

#if defined(HAVE_TIMER_CREATE) && defined(SIGEV_THREAD)
#define HAVE_TIMEOUT_OPTION 1
#endif

/* Server supports API versions between 1 and MAX_API_VERSION */
#define MAX_API_VERSION 2

#define NBDKIT_API_VERSION MAX_API_VERSION
#define NBDKIT_INTERNAL
#include "nbdkit-plugin.h"
#include "nbdkit-filter.h"
#include "cleanup.h"
#include "nbd-protocol.h"
#include "string-vector.h"
#include "unix-path-max.h"
#include "vector.h"
#include "windows-compat.h"

/* Define unlikely macro, but only for GCC.  These are used to move
 * debug and error handling code out of hot paths.
 */
#if defined (__GNUC__)
#define unlikely(x) __builtin_expect (!!(x), 0)
#define if_verbose if (unlikely (verbose))
#else
#define unlikely(x) (x)
#define if_verbose if (verbose)
#endif

#if defined (__SANITIZE_ADDRESS__)
# define DO_DLCLOSE 0
#elif ENABLE_LIBFUZZER
/* XXX This causes dlopen in the server to leak during fuzzing.
 * However it is necessary because of
 * https://bugs.llvm.org/show_bug.cgi?id=43917
 */
# define DO_DLCLOSE 0
#else
# if HAVE_VALGRIND
#  include <valgrind.h>
/* http://valgrind.org/docs/manual/faq.html#faq.unhelpful */
#  define DO_DLCLOSE !RUNNING_ON_VALGRIND
# else
#  define DO_DLCLOSE 1
# endif
#endif

/* Declare program_name. */
#if HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME == 1
#include <errno.h>
#define program_name program_invocation_short_name
#else
#define program_name "nbdkit"
#endif

#define container_of(ptr, type, member) ({                       \
      const typeof (((type *) 0)->member) *__mptr = (ptr);       \
      (type *) ((char *) __mptr - offsetof (type, member));      \
    })

/* Maximum read or write request that we will handle. */
#define MAX_REQUEST_SIZE (64 * 1024 * 1024)

/* main.c */
enum log_to {
  LOG_TO_DEFAULT,        /* --log not specified: log to stderr, unless
                            we forked into the background in which
                            case log to syslog */
  LOG_TO_STDERR,         /* --log=stderr forced on the command line */
  LOG_TO_SYSLOG,         /* --log=syslog forced on the command line */
  LOG_TO_NULL,           /* --log=null forced on the command line */
  LOG_TO_FILE,           /* --log=/path used on the command line */
};

enum service_mode {
  /* These two modes cannot form an NBD URI: */
  SERVICE_MODE_SOCKET_ACTIVATION, /* socket activation. */
  SERVICE_MODE_LISTEN_STDIN,      /* -s */

  SERVICE_MODE_UNIXSOCKET,        /* -U */
  SERVICE_MODE_VSOCK,             /* --vsock */
  SERVICE_MODE_TCPIP,             /* --port */
};
extern const char *service_mode_string (enum service_mode);

extern int tcpip_sock_af;
extern struct debug_flag *debug_flags;
extern const char *export_name;
extern bool foreground;
extern const char *ipaddr;
extern bool keepalive;
extern enum log_to log_to;
extern const char *log_to_file;
extern FILE *log_to_fp;
extern unsigned mask_handshake;
extern const char *process_name;
extern bool newstyle;
extern bool no_mc;
extern bool no_sr;
extern const char *port;
extern bool print_uri;
extern bool read_only;
extern const char *run;
extern bool listen_stdin;
extern const char *selinux_label;
extern unsigned threads;
extern unsigned timeout_secs, timeout_nsecs;
extern int tls;
extern const char *tls_certificates_dir;
extern const char *tls_priority;
extern const char *tls_psk;
extern bool tls_verify_peer;
extern char *unixsocket;
extern const char *user, *group;
extern bool verbose;
extern bool vsock;

extern enum service_mode service_mode;
extern char *uri;
extern bool configured;
extern int saved_stdin;
extern int saved_stdout;

extern struct backend *top;
#define for_each_backend(b) for (b = top; b != NULL; b = b->next)

/* debug.c */
#define debug(fs, ...)                                   \
  do {                                                   \
    if_verbose                                           \
      debug_in_server ((fs), ##__VA_ARGS__);             \
  } while (0)

extern void debug_in_server (const char *msg, ...)
  NBDKIT_ATTRIBUTE_FORMAT_PRINTF (1, 2);

#define nbdkit_debug _Pragma("GCC error \"Use debug() instead of nbdkit_debug() inside the server\"")

/* quit.c */
extern volatile int quit;
#ifndef WIN32
extern int quit_fd;
#else
extern HANDLE quit_fd;
#endif
extern void set_up_quit_pipe (void);
extern void close_quit_pipe (void);
extern void handle_quit (int sig);

/* signals.c */
extern void set_up_signals (void);

/* background.c */
extern bool forked_into_background;
extern void fork_into_background (void);

/* captive.c */
extern void run_command (void);

/* socket-activation.c */
struct socket_activation_socket {
  int fd;
  char *name;
};
DEFINE_VECTOR_TYPE (socket_activation_list, struct socket_activation_socket);
extern void get_socket_activation (void);
extern void free_socket_activation (void);
extern socket_activation_list socket_activation;

/* timeout.c */
struct connection;
extern int start_timeout (struct connection *conn)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern void cancel_timeout (struct connection *conn)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

/* usergroup.c */
extern void change_user (void);

/* uri.c */
extern char *make_uri (void);

/* connections.c */

/* Flags for connection_send_function */
enum {
  SEND_MORE = 1, /* Hint to use MSG_MORE/corking to group send()s */
};

typedef int (*connection_recv_function) (void *buf, size_t len)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
typedef int (*connection_send_function) (const void *buf, size_t len,
                                         int flags)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
typedef void (*connection_close_function) (int how);

/* struct context stores data per connection and backend.  Primarily
 * this is the filter or plugin handle, but other state is also stored
 * here.
 */
enum {
  HANDLE_OPEN = 1,      /* Set if .open passed, so .close is needed */
  HANDLE_CONNECTED = 2, /* Set if .prepare passed, so .finalize is needed */
  HANDLE_FAILED = 4,    /* Set if .finalize failed */
};

struct context {
  struct nbdkit_next_ops next;  /* Must be first member, for ABI reasons */

  uint64_t magic;               /* Magic number used to validate struct. */
#define CONTEXT_MAGIC 0xc011

  void *handle;         /* Plugin or filter handle. */
  struct backend *b;    /* Backend that provided handle. */
  struct context *c_next; /* Underlying context, only when b->next != NULL. */
  struct connection *conn; /* Active connection at context creation, if any. */

  char *exportname;     /* Exportname, either from client or at
                           context creation. */

  unsigned char state;  /* Bitmask of HANDLE_* values */

  uint64_t exportsize;
  uint32_t minimum_block_size;
  uint32_t preferred_block_size;
  uint32_t maximum_block_size;
  int can_write;
  int can_flush;
  int is_rotational;
  int can_trim;
  int can_zero;
  int can_fast_zero;
  int can_fua;
  int can_multi_conn;
  int can_extents;
  int can_cache;
};

typedef enum {
  STATUS_DEAD,         /* Connection is closed */
  STATUS_CLIENT_DONE,  /* Client has sent NBD_CMD_DISC */
  STATUS_SHUTDOWN,     /* Server wants soft shutdown */
  STATUS_ACTIVE,       /* Client can make requests */
} conn_status;

struct connection {
  uint64_t magic;               /* Magic number used to validate struct. */
#define CONN_MAGIC 0xc05

  /* Listed in precedence order: do not grab earlier locks in this list
   * while holding a later lock.
   */
  pthread_mutex_t request_lock; /* Forces serialization of requests */
  pthread_mutex_t read_lock; /* Read entire client payload off wire */
  pthread_mutex_t write_lock; /* Protect sockout, write response to wire */
  pthread_mutex_t status_lock; /* Track current status of client */

  conn_status status;
  int status_pipe[2]; /* track status changes via poll when nworkers > 1 */
  void *crypto_session;
  int nworkers;

  struct context *top_context;  /* The context tied to 'top'. */
  char **default_exportname;    /* One per plugin and filter. */

  uint32_t cflags;
  uint16_t eflags;
  bool handshake_complete;
#ifdef HAVE_TIMEOUT_OPTION
  timer_t timer;
  bool timer_set;
#endif
  bool using_tls;
  bool structured_replies;
  bool meta_context_base_allocation;

  string_vector interns;
  char *exportname_from_set_meta_context;

  int sockin, sockout;
  /* If nworkers > 1, only call this while read_lock is held */
  connection_recv_function recv;
  /* If nworkers > 1, only call these while write_lock is held */
  connection_send_function send;
  connection_close_function close;
};

extern void handle_single_connection (int sockin, int sockout);
extern conn_status connection_get_status (void);
extern bool connection_set_status (conn_status value);

/* protocol-handshake.c */
extern int protocol_handshake (void);
extern int protocol_common_open (uint64_t *exportsize, uint16_t *flags,
                                 const char *exportname)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 3));

/* protocol-handshake-oldstyle.c */
extern int protocol_handshake_oldstyle (void);

/* protocol-handshake-newstyle.c */
extern int protocol_handshake_newstyle (void);

/* protocol.c */
extern bool protocol_recv_request_send_reply (void);

/* The context ID of base:allocation.  As far as I can tell it doesn't
 * matter what this is as long as nbdkit always returns the same
 * number.
 */
#define base_allocation_id 1

/* public.c */
extern void free_interns (void);

/* crypto.c */
#define root_tls_certificates_dir sysconfdir "/pki/" PACKAGE_NAME
extern void crypto_init (bool tls_set_on_cli);
extern void crypto_free (void);
extern int crypto_negotiate_tls (int sockin, int sockout);

/* debug-flags.c */
extern void add_debug_flag (const char *arg);
extern void apply_debug_flags (void *dl, const char *name);
extern void free_debug_flags (void);

/* log.c */
extern void log_verror (const char *fs, va_list args);

/* log-*.c */
extern void log_fp_verror (FILE *fp,
                           int orig_errno, const char *fs, va_list args)
  NBDKIT_ATTRIBUTE_FORMAT_PRINTF (3, 0);
extern void log_syslog_verror (int orig_errno, const char *fs, va_list args)
  NBDKIT_ATTRIBUTE_FORMAT_PRINTF (2, 0);

/* vfprintf.c */
#if !HAVE_VFPRINTF_PERCENT_M
#include <stdio.h>
#define vfprintf replace_vfprintf
extern int replace_vfprintf (FILE *f, const char *fmt, va_list args)
  __attribute__ ((__format__ (printf, 2, 0)));
#endif

/* backend.c */
struct backend {
  uint64_t magic;               /* Magic number used to validate struct. */
#define BACKEND_MAGIC 0xbac

  /* Next filter or plugin in the chain.  This is always NULL for
   * plugins and never NULL for filters.
   */
  struct backend *next;

  /* A unique index used to fetch the handle from the connections
   * object.  The plugin (last in the chain) has index 0, and the
   * filters have index 1, 2, ... depending how "far" they are from
   * the plugin.
   */
  size_t i;

  /* The type of backend: filter or plugin. */
  const char *type;

  /* A copy of the backend name that survives a dlclose. */
  char *name;

  /* The file the backend was loaded from. */
  char *filename;

  /* The dlopen handle for the backend. */
  void *dl;

  /* Backend callbacks. All are required. */
  void (*free) (struct backend *);
  int (*thread_model) (struct backend *);
  const char *(*plugin_name) (struct backend *);
  void (*usage) (struct backend *);
  const char *(*version) (struct backend *);
  void (*dump_fields) (struct backend *);
  void (*config) (struct backend *, const char *key, const char *value);
  void (*config_complete) (struct backend *);
  const char *(*magic_config_key) (struct backend *);
  void (*get_ready) (struct backend *);
  void (*after_fork) (struct backend *);
  void (*cleanup) (struct backend *);

  int (*preconnect) (struct backend *, int readonly);
  int (*list_exports) (struct backend *, int readonly, int is_tls,
                       struct nbdkit_exports *exports);
  const char *(*default_export) (struct backend *, int readonly, int is_tls);
  void *(*open) (struct context *, int readonly, const char *exportname,
                 int is_tls);
  int (*prepare) (struct context *, int readonly);
  int (*finalize) (struct context *);
  void (*close) (struct context *);

  const char *(*export_description) (struct context *);
  int64_t (*get_size) (struct context *);
  int (*block_size) (struct context *,
                     uint32_t *minimum, uint32_t *preferred, uint32_t *maximum);
  int (*can_write) (struct context *);
  int (*can_flush) (struct context *);
  int (*is_rotational) (struct context *);
  int (*can_trim) (struct context *);
  int (*can_zero) (struct context *);
  int (*can_fast_zero) (struct context *);
  int (*can_extents) (struct context *);
  int (*can_fua) (struct context *);
  int (*can_multi_conn) (struct context *);
  int (*can_cache) (struct context *);

  int (*pread) (struct context *,
                void *buf, uint32_t count, uint64_t offset,
                uint32_t flags, int *err);
  int (*pwrite) (struct context *,
                 const void *buf, uint32_t count, uint64_t offset,
                 uint32_t flags, int *err);
  int (*flush) (struct context *, uint32_t flags, int *err);
  int (*trim) (struct context *,
               uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*zero) (struct context *,
               uint32_t count, uint64_t offset, uint32_t flags, int *err);
  int (*extents) (struct context *,
                  uint32_t count, uint64_t offset, uint32_t flags,
                  struct nbdkit_extents *extents, int *err);
  int (*cache) (struct context *,
                uint32_t count, uint64_t offset, uint32_t flags, int *err);
};

extern void backend_init (struct backend *b, struct backend *next, size_t index,
                          const char *filename, void *dl, const char *type)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 4, 5, 6));
extern void backend_load (struct backend *b, const char *name,
                          void (*load) (void))
  NBDKIT_ATTRIBUTE_NONNULL ((1 /* not 2 */));
extern void backend_unload (struct backend *b, void (*unload) (void))
  NBDKIT_ATTRIBUTE_NONNULL ((1));

extern int backend_list_exports (struct backend *b, int readonly,
                                 struct nbdkit_exports *exports)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3));
extern const char *backend_default_export (struct backend *b, int readonly)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
/* exportname is only valid for this call and almost certainly will be
 * freed on return of this function, so backends must save the
 * exportname if they need to refer to it later.
 */
extern struct context *backend_open (struct backend *b,
                                     int readonly, const char *exportname,
                                     int shared)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3));
extern int backend_prepare (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_finalize (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern void backend_close (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern bool backend_valid_range (struct context *c,
                                 uint64_t offset, uint32_t count)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

extern const char *backend_export_description (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int64_t backend_get_size (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_block_size (struct context *c,
                               uint32_t *minimum, uint32_t *preferred,
                               uint32_t *maximum)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 3, 4));
extern int backend_can_write (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_flush (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_is_rotational (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_trim (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_zero (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_fast_zero (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_extents (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_fua (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_multi_conn (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern int backend_can_cache (struct context *c)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

extern int backend_pread (struct context *c,
                          void *buf, uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 6));
extern int backend_pwrite (struct context *c,
                           const void *buf, uint32_t count, uint64_t offset,
                           uint32_t flags, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 2, 6));
extern int backend_flush (struct context *c,
                          uint32_t flags, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3));
extern int backend_trim (struct context *c,
                         uint32_t count, uint64_t offset, uint32_t flags,
                         int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 5));
extern int backend_zero (struct context *c,
                         uint32_t count, uint64_t offset, uint32_t flags,
                         int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 5));
extern int backend_extents (struct context *c,
                            uint32_t count, uint64_t offset, uint32_t flags,
                            struct nbdkit_extents *extents, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 5, 6));
extern int backend_cache (struct context *c,
                          uint32_t count, uint64_t offset,
                          uint32_t flags, int *err)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 5));

/* plugins.c */
typedef struct nbdkit_plugin *(*plugin_init_function) (void);
extern struct backend *plugin_register (size_t index, const char *filename,
                                        void *dl,
                                        plugin_init_function plugin_init)
  NBDKIT_ATTRIBUTE_NONNULL ((2, 3, 4));

/* filters.c */
typedef struct nbdkit_filter *(*filter_init_function) (void);
extern struct backend *filter_register (struct backend *next, size_t index,
                                        const char *filename, void *dl,
                                        filter_init_function filter_init)
  NBDKIT_ATTRIBUTE_NONNULL ((1, 3, 4, 5));

/* locks.c */
extern unsigned thread_model;
extern void lock_init_thread_model (void);
extern const char *name_of_thread_model (int model);
extern void lock_connection (void);
extern void unlock_connection (void);
extern void lock_request (void);
extern void unlock_request (void);
extern void lock_unload (void);
extern void unlock_unload (void);

/* sockets.c */
DEFINE_VECTOR_TYPE (sockets, int);
extern void bind_unix_socket (sockets *) NBDKIT_ATTRIBUTE_NONNULL ((1));
extern void bind_tcpip_socket (sockets *) NBDKIT_ATTRIBUTE_NONNULL ((1));
extern void bind_vsock (sockets *) NBDKIT_ATTRIBUTE_NONNULL ((1));
extern void accept_incoming_connections (const sockets *socks)
  NBDKIT_ATTRIBUTE_NONNULL ((1));

/* threadlocal.c */
extern void threadlocal_init (void);
extern void threadlocal_new_server_thread (void);
extern void threadlocal_set_name (const char *name)
  NBDKIT_ATTRIBUTE_NONNULL ((1));
extern const char *threadlocal_get_name (void);
extern void threadlocal_set_instance_num (size_t instance_num);
extern size_t threadlocal_get_instance_num (void);
extern void threadlocal_set_errno (int err);
extern int threadlocal_get_errno (void);
extern void threadlocal_set_last_error (char *msg);
extern void threadlocal_clear_last_error (void);
extern const char *threadlocal_get_last_error (void);
extern int threadlocal_set_timestamp (char *timestamp);
extern void *threadlocal_buffer (size_t size);
extern void threadlocal_set_conn (struct connection *conn);
extern struct connection *threadlocal_get_conn (void);
extern struct context *threadlocal_get_context (void);

extern struct context *threadlocal_push_context (struct context *ctx);
extern void threadlocal_pop_context (struct context **ctx);
#define CLEANUP_CONTEXT_POP __attribute__ ((cleanup (threadlocal_pop_context)))
#define PUSH_CONTEXT_FOR_SCOPE(ctx)                                     \
  CLEANUP_CONTEXT_POP CLANG_UNUSED_VARIABLE_WORKAROUND                  \
  struct context *NBDKIT_UNIQUE_NAME (_ctx) = threadlocal_push_context (ctx)

/* Macro which sets local variable struct connection *conn from
 * thread-local storage, asserting that it is non-NULL.  If you want
 * to check if conn could be NULL (eg. outside a connection context)
 * then call threadlocal_get_conn instead.
 */
#define GET_CONN                                        \
  struct connection *conn = threadlocal_get_conn ();    \
  assert (conn != NULL)

/* exports.c */
extern int exports_resolve_default (struct nbdkit_exports *exports,
                                    struct backend *b, int readonly);

#endif /* NBDKIT_INTERNAL_H */
