/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
 * All rights reserved.
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
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>

#include <dlfcn.h>

#include "internal.h"
#include "strndup.h"
#include "syslog.h"
#include "options.h"
#include "exit-with-parent.h"

static char *make_random_fifo (void);
static struct backend *open_plugin_so (size_t i, const char *filename, int short_name);
static struct backend *open_filter_so (struct backend *next, size_t i, const char *filename, int short_name);
static void start_serving (void);
static void write_pidfile (void);
static int is_config_key (const char *key, size_t len);

struct debug_flag *debug_flags; /* -D */
bool exit_with_parent;          /* --exit-with-parent */
const char *exportname;         /* -e */
bool foreground;                /* -f */
const char *ipaddr;             /* -i */
enum log_to log_to = LOG_TO_DEFAULT; /* --log */
bool newstyle = true;           /* false = -o, true = -n */
char *pidfile;                  /* -P */
const char *port;               /* -p */
bool readonly;                  /* -r */
const char *run;                /* --run */
bool listen_stdin;              /* -s */
const char *selinux_label;      /* --selinux-label */
int threads;                    /* -t */
int tls;                        /* --tls : 0=off 1=on 2=require */
const char *tls_certificates_dir; /* --tls-certificates */
const char *tls_psk;            /* --tls-psk */
bool tls_verify_peer;           /* --tls-verify-peer */
char *unixsocket;               /* -U */
const char *user, *group;       /* -u & -g */
bool verbose;                   /* -v */
unsigned int socket_activation  /* $LISTEN_FDS and $LISTEN_PID set */;

/* The currently loaded plugin. */
struct backend *backend;

static char *random_fifo_dir = NULL;
static char *random_fifo = NULL;

static void
usage (void)
{
  /* --{short,long}-options remain undocumented */
  printf (
#include "synopsis.c"
  );
  printf ("\n"
          "Please read the nbdkit(1) manual page for full usage.\n");
}

static void
display_version (void)
{
  printf ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static void
dump_config (void)
{
  printf ("%s=%s\n", "bindir", bindir);
  printf ("%s=%s\n", "filterdir", filterdir);
  printf ("%s=%s\n", "libdir", libdir);
  printf ("%s=%s\n", "mandir", mandir);
  printf ("%s=%s\n", "name", PACKAGE_NAME);
  printf ("%s=%s\n", "plugindir", plugindir);
  printf ("%s=%s\n", "root_tls_certificates_dir", root_tls_certificates_dir);
  printf ("%s=%s\n", "sbindir", sbindir);
#ifdef HAVE_LIBSELINUX
  printf ("selinux=yes\n");
#else
  printf ("selinux=no\n");
#endif
  printf ("%s=%s\n", "sysconfdir", sysconfdir);
#ifdef HAVE_GNUTLS
  printf ("tls=yes\n");
#else
  printf ("tls=no\n");
#endif
  printf ("%s=%s\n", "version", PACKAGE_VERSION);
}

int
main (int argc, char *argv[])
{
  int c;
  bool help = false, version = false, dump_plugin = false;
  int tls_set_on_cli = false;
  int short_name;
  const char *filename;
  char *p;
  static struct filter_filename {
    struct filter_filename *next;
    const char *filename;
  } *filter_filenames = NULL;
  size_t i;
  const char *magic_config_key;

  threadlocal_init ();

  /* The default setting for TLS depends on whether we were
   * compiled with GnuTLS.
   */
#ifdef HAVE_GNUTLS
  tls = 1;
#else
  tls = 0;
#endif

  /* Returns 0 if no socket activation, or the number of FDs. */
  socket_activation = get_socket_activation ();

  for (;;) {
    c = getopt_long (argc, argv, short_options, long_options, NULL);
    if (c == -1)
      break;

    switch (c) {
    case 'D':
      {
        const char *p, *q;
        struct debug_flag *flag;

        /* Debug Flag must be "NAME.FLAG=N".
         *                     ^    ^    ^
         *                optarg    p    q  (after +1 adjustment below)
         */
        p = strchr (optarg, '.');
        q = strchr (optarg, '=');
        if (p == NULL || q == NULL) {
        bad_debug_flag:
          fprintf (stderr,
                   "%s: -D (Debug Flag) must have the format NAME.FLAG=N\n",
                   program_name);
          exit (EXIT_FAILURE);
        }
        p++;                    /* +1 adjustment */
        q++;

        if (p - optarg <= 1) goto bad_debug_flag; /* NAME too short */
        if (p > q) goto bad_debug_flag;
        if (q - p <= 1) goto bad_debug_flag; /* FLAG too short */
        if (*q == '\0') goto bad_debug_flag; /* N too short */

        flag = malloc (sizeof *flag);
        if (flag == NULL) {
        debug_flag_perror:
          perror ("malloc");
          exit (EXIT_FAILURE);
        }
        flag->name = strndup (optarg, p-optarg-1);
        if (!flag->name) goto debug_flag_perror;
        flag->flag = strndup (p, q-p-1);
        if (!flag->flag) goto debug_flag_perror;
        if (sscanf (q, "%d", &flag->value) != 1) goto bad_debug_flag;
        flag->used = false;

        /* Add flag to the linked list. */
        flag->next = debug_flags;
        debug_flags = flag;
      }
      break;

    case DUMP_CONFIG_OPTION:
      dump_config ();
      exit (EXIT_SUCCESS);

    case DUMP_PLUGIN_OPTION:
      dump_plugin = true;
      break;

    case EXIT_WITH_PARENT_OPTION:
#ifdef HAVE_EXIT_WITH_PARENT
      exit_with_parent = true;
      foreground = true;
      break;
#else
      fprintf (stderr,
               "%s: --exit-with-parent is not implemented "
               "for this operating system\n",
               program_name);
      exit (EXIT_FAILURE);
#endif

    case FILTER_OPTION:
      {
        struct filter_filename *t;

        t = malloc (sizeof *t);
        if (t == NULL) {
          perror ("malloc");
          exit (EXIT_FAILURE);
        }
        t->next = filter_filenames;
        t->filename = optarg;
        filter_filenames = t;
      }
      break;

    case LOG_OPTION:
      if (strcmp (optarg, "stderr") == 0)
        log_to = LOG_TO_STDERR;
      else if (strcmp (optarg, "syslog") == 0)
        log_to = LOG_TO_SYSLOG;
      else {
        fprintf (stderr, "%s: --log must be \"stderr\" or \"syslog\"\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      break;

    case LONG_OPTIONS_OPTION:
      for (i = 0; long_options[i].name != NULL; ++i) {
        if (strcmp (long_options[i].name, "long-options") != 0 &&
            strcmp (long_options[i].name, "short-options") != 0)
          printf ("--%s\n", long_options[i].name);
      }
      exit (EXIT_SUCCESS);

    case RUN_OPTION:
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with --run flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      run = optarg;
      foreground = true;
      break;

    case SELINUX_LABEL_OPTION:
      selinux_label = optarg;
      break;

    case SHORT_OPTIONS_OPTION:
      for (i = 0; short_options[i]; ++i) {
        if (short_options[i] != ':')
          printf ("-%c\n", short_options[i]);
      }
      exit (EXIT_SUCCESS);

    case TLS_OPTION:
      tls_set_on_cli = true;
      if (strcasecmp (optarg, "require") == 0 ||
          strcasecmp (optarg, "required") == 0 ||
          strcasecmp (optarg, "force") == 0)
        tls = 2;
      else {
        tls = nbdkit_parse_bool (optarg);
        if (tls == -1)
          exit (EXIT_FAILURE);
      }
      break;

    case TLS_CERTIFICATES_OPTION:
      tls_certificates_dir = optarg;
      break;

    case TLS_PSK_OPTION:
      tls_psk = optarg;
      break;

    case TLS_VERIFY_PEER_OPTION:
      tls_verify_peer = true;
      break;

    case 'e':
      exportname = optarg;
      newstyle = true;
      break;

    case 'f':
      foreground = true;
      break;

    case 'g':
      group = optarg;
      break;

    case 'i':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -i flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      ipaddr = optarg;
      break;

    case 'n':
      newstyle = true;
      break;

    case 'o':
      newstyle = false;
      break;

    case 'P':
      pidfile = nbdkit_absolute_path (optarg);
      if (pidfile == NULL)
        exit (EXIT_FAILURE);
      break;

    case 'p':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -p flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      port = optarg;
      break;

    case 'r':
      readonly = true;
      break;

    case 's':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -s flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      listen_stdin = true;
      break;

    case 't':
      {
        char *end;

        errno = 0;
        threads = strtoul (optarg, &end, 0);
        if (errno || *end) {
          fprintf (stderr, "%s: cannot parse '%s' into threads\n",
                   program_name, optarg);
          exit (EXIT_FAILURE);
        }
        /* XXX Worth a maximimum limit on threads? */
      }
      break;

    case 'U':
      if (socket_activation) {
        fprintf (stderr, "%s: cannot use socket activation with -U flag\n",
                 program_name);
        exit (EXIT_FAILURE);
      }
      if (strcmp (optarg, "-") == 0)
        unixsocket = make_random_fifo ();
      else
        unixsocket = nbdkit_absolute_path (optarg);
      if (unixsocket == NULL)
        exit (EXIT_FAILURE);
      break;

    case 'u':
      user = optarg;
      break;

    case 'v':
      verbose = true;
      break;

    case 'V':
      version = true;
      break;

    case HELP_OPTION:
      help = true;
      break;

    default:
      usage ();
      exit (EXIT_FAILURE);
    }
  }

  /* No extra parameters. */
  if (optind >= argc) {
    if (help) {
      usage ();
      exit (EXIT_SUCCESS);
    }
    if (version) {
      display_version ();
      exit (EXIT_SUCCESS);
    }
    if (dump_plugin) {
      /* Incorrect use of --dump-plugin. */
      fprintf (stderr,
               "%s: use 'nbdkit plugin --dump-plugin' or\n"
               "'nbdkit /path/to/plugin.so --dump-plugin'\n",
               program_name);
      exit (EXIT_FAILURE);
    }

    /* Otherwise this is an error. */
    fprintf (stderr,
             "%s: no plugins given on the command line.\n"
             "Use '%s --help' or "
             "read the nbdkit(1) manual page for documentation.\n",
             program_name, program_name);
    exit (EXIT_FAILURE);
  }

  /* Oldstyle protocol + exportname not allowed. */
  if (!newstyle && exportname != NULL) {
    fprintf (stderr,
             "%s: cannot use oldstyle protocol (-o) and exportname (-e)\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* If exportname was not set on the command line, use "". */
  if (exportname == NULL)
    exportname = "";

  /* --tls=require and oldstyle won't work. */
  if (tls == 2 && !newstyle) {
    fprintf (stderr,
             "%s: cannot use oldstyle protocol (-o) and require TLS\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Set the umask to a known value.  This makes the behaviour of
   * plugins when creating files more predictable, and also removes an
   * implicit dependency on umask when calling mkstemp(3).
   */
  umask (0022);

  /* If we will or might use syslog. */
  if (log_to == LOG_TO_SYSLOG || log_to == LOG_TO_DEFAULT)
    openlog (program_name, LOG_PID, 0);

  /* Initialize TLS. */
  crypto_init (tls_set_on_cli);
  assert (tls != -1);

  /* Implement --exit-with-parent early in case plugin initialization
   * takes a long time and the parent exits during that time.
   */
#ifdef HAVE_EXIT_WITH_PARENT
  if (exit_with_parent) {
    if (set_exit_with_parent () == -1) {
      perror ("nbdkit: --exit-with-parent");
      exit (EXIT_FAILURE);
    }
  }
#endif

  /* The remaining command line arguments are the plugin name and
   * parameters.  If --help, --version or --dump-plugin were specified
   * then we open the plugin so that we can display the per-plugin
   * help/version/plugin information.
   */
  filename = argv[optind++];
  short_name = is_short_name (filename);

  /* Is there an executable script located in the plugindir?
   * If so we simply execute it with the current command line.
   */
  if (short_name) {
    size_t i;
    struct stat statbuf;
    CLEANUP_FREE char *script;

    if (asprintf (&script,
                  "%s/nbdkit-%s-plugin", plugindir, filename) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }

    if (stat (script, &statbuf) == 0 &&
        (statbuf.st_mode & S_IXUSR) != 0) {
      /* We're going to execute the plugin directly.
       * Replace argv[0] with argv[optind-1] and move further arguments
       * down the list.
       */
      argv[0] = argv[optind-1];
      for (i = optind; i <= argc; i++)
        argv[i-1] = argv[i];
      execv (script, argv);
      perror (script);
      exit (EXIT_FAILURE);
    }
  }

  /* Open the plugin (first) and then wrap the plugin with the
   * filters.  The filters are wrapped in reverse order that they
   * appear on the command line so that in the end ‘backend’ points to
   * the first filter on the command line.
   */
  backend = open_plugin_so (0, filename, short_name);
  i = 1;
  while (filter_filenames) {
    struct filter_filename *t = filter_filenames;
    const char *filename = t->filename;
    int short_name = is_short_name (filename);

    backend = open_filter_so (backend, i++, filename, short_name);

    filter_filenames = t->next;
    free (t);
  }

  /* Check all debug flags were used, and free them. */
  while (debug_flags != NULL) {
    struct debug_flag *next = debug_flags->next;

    if (!debug_flags->used) {
      fprintf (stderr, "%s: debug flag -D %s.%s was not used\n",
               program_name, debug_flags->name, debug_flags->flag);
      exit (EXIT_FAILURE);
    }
    free (debug_flags->name);
    free (debug_flags->flag);
    free (debug_flags);
    debug_flags = next;
  }

  /* Select a thread model. */
  lock_init_thread_model ();

  if (help) {
    struct backend *b;

    usage ();
    for_each_backend (b) {
      printf ("\n");
      b->usage (b);
    }
    backend->free (backend);
    exit (EXIT_SUCCESS);
  }

  if (version) {
    const char *v;
    struct backend *b;

    display_version ();
    for_each_backend (b) {
      printf ("%s", b->name (b));
      if ((v = b->version (b)) != NULL)
        printf (" %s", v);
      printf ("\n");
    }
    backend->free (backend);
    exit (EXIT_SUCCESS);
  }

  /* Call config and config_complete to parse the parameters.
   *
   * If the plugin provides magic_config_key then any "bare" values
   * (ones not containing "=") are prefixed with this key.
   *
   * For backwards compatibility with old plugins, and to support
   * scripting languages, if magic_config_key == NULL then if the
   * first parameter is bare it is prefixed with the key "script", and
   * any other bare parameters are errors.
   */
  magic_config_key = backend->magic_config_key (backend);
  for (i = 0; optind < argc; ++i, ++optind) {
    p = strchr (argv[optind], '=');
    if (p && is_config_key (argv[optind], p - argv[optind])) { /* key=value */
      *p = '\0';
      backend->config (backend, argv[optind], p+1);
    }
    else if (magic_config_key == NULL) {
      if (i == 0)               /* magic script parameter */
        backend->config (backend, "script", argv[optind]);
      else {
        fprintf (stderr,
                 "%s: expecting key=value on the command line but got: %s\n",
                 program_name, argv[optind]);
        exit (EXIT_FAILURE);
      }
    }
    else {                      /* magic config key */
      backend->config (backend, magic_config_key, argv[optind]);
    }
  }

  /* This must run after parsing the parameters so that the script can
   * be loaded for scripting languages.  But it must be called before
   * config_complete so that the plugin doesn't check for missing
   * parameters.
   */
  if (dump_plugin) {
    backend->dump_fields (backend);
    backend->free (backend);
    exit (EXIT_SUCCESS);
  }

  backend->config_complete (backend);

  start_serving ();

  backend->free (backend);
  backend = NULL;

  free (unixsocket);
  free (pidfile);

  if (random_fifo) {
    unlink (random_fifo);
    free (random_fifo);
  }

  if (random_fifo_dir) {
    rmdir (random_fifo_dir);
    free (random_fifo_dir);
  }

  crypto_free ();

  exit (EXIT_SUCCESS);
}

/* Implementation of '-U -' */
static char *
make_random_fifo (void)
{
  char template[] = "/tmp/nbdkitXXXXXX";
  char *unixsocket;

  if (mkdtemp (template) == NULL) {
    perror ("mkdtemp");
    return NULL;
  }

  random_fifo_dir = strdup (template);
  if (random_fifo_dir == NULL) {
    perror ("strdup");
    return NULL;
  }

  if (asprintf (&random_fifo, "%s/socket", template) == -1) {
    perror ("asprintf");
    return NULL;
  }

  unixsocket = strdup (random_fifo);
  if (unixsocket == NULL) {
    perror ("strdup");
    return NULL;
  }

  return unixsocket;
}

static struct backend *
open_plugin_so (size_t i, const char *name, int short_name)
{
  struct backend *ret;
  char *filename = (char *) name;
  bool free_filename = false;
  void *dl;
  struct nbdkit_plugin *(*plugin_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the plugindir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-plugin.so", plugindir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = true;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr,
             "%s: error: cannot open plugin '%s': %s\n"
             "Use '%s --help' or "
             "read the nbdkit(1) manual page for documentation.\n",
             program_name, name, dlerror (),
             program_name);
    exit (EXIT_FAILURE);
  }

  /* Initialize the plugin.  See dlopen(3) to understand C weirdness. */
  dlerror ();
  *(void **) (&plugin_init) = dlsym (dl, "plugin_init");
  if ((error = dlerror ()) != NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, name, error);
    exit (EXIT_FAILURE);
  }
  if (!plugin_init) {
    fprintf (stderr, "%s: %s: invalid plugin_init\n", program_name, name);
    exit (EXIT_FAILURE);
  }

  /* Register the plugin. */
  ret = plugin_register (i, filename, dl, plugin_init);

  if (free_filename)
    free (filename);

  return ret;
}

static struct backend *
open_filter_so (struct backend *next, size_t i,
                const char *name, int short_name)
{
  struct backend *ret;
  char *filename = (char *) name;
  bool free_filename = false;
  void *dl;
  struct nbdkit_filter *(*filter_init) (void);
  char *error;

  if (short_name) {
    /* Short names are rewritten relative to the filterdir. */
    if (asprintf (&filename,
                  "%s/nbdkit-%s-filter.so", filterdir, name) == -1) {
      perror ("asprintf");
      exit (EXIT_FAILURE);
    }
    free_filename = true;
  }

  dl = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
  if (dl == NULL) {
    fprintf (stderr, "%s: error: cannot open filter '%s': %s\n",
             program_name, name, dlerror ());
    exit (EXIT_FAILURE);
  }

  /* Initialize the filter.  See dlopen(3) to understand C weirdness. */
  dlerror ();
  *(void **) (&filter_init) = dlsym (dl, "filter_init");
  if ((error = dlerror ()) != NULL) {
    fprintf (stderr, "%s: %s: %s\n", program_name, name, error);
    exit (EXIT_FAILURE);
  }
  if (!filter_init) {
    fprintf (stderr, "%s: %s: invalid filter_init\n", program_name, name);
    exit (EXIT_FAILURE);
  }

  /* Register the filter. */
  ret = filter_register (next, i, filename, dl, filter_init);

  if (free_filename)
    free (filename);

  return ret;
}

static void
start_serving (void)
{
  int *socks;
  size_t nr_socks;
  size_t i;

  /* If the user has mixed up -p/-U/-s options, then give an error.
   *
   * XXX Actually the server could easily be extended to handle both
   * TCP/IP and Unix sockets, or even multiple TCP/IP ports.
   */
  if ((port && unixsocket) || (port && listen_stdin) ||
      (unixsocket && listen_stdin) || (listen_stdin && run)) {
    fprintf (stderr,
             "%s: -p, -U and -s options cannot appear at the same time\n",
             program_name);
    exit (EXIT_FAILURE);
  }

  set_up_quit_pipe ();
  set_up_signals ();

  /* Socket activation -- we are handling connections on pre-opened
   * file descriptors [FIRST_SOCKET_ACTIVATION_FD ..
   * FIRST_SOCKET_ACTIVATION_FD+nr_socks-1].
   */
  if (socket_activation) {
    nr_socks = socket_activation;
    debug ("using socket activation, nr_socks = %zu", nr_socks);
    socks = malloc (sizeof (int) * nr_socks);
    if (socks == NULL) {
      perror ("malloc");
      exit (EXIT_FAILURE);
    }
    for (i = 0; i < nr_socks; ++i)
      socks[i] = FIRST_SOCKET_ACTIVATION_FD + i;
    change_user ();
    write_pidfile ();
    accept_incoming_connections (socks, nr_socks);
    free_listening_sockets (socks, nr_socks); /* also closes them */
    return;
  }

  /* Handling a single connection on stdin/stdout. */
  if (listen_stdin) {
    change_user ();
    write_pidfile ();
    threadlocal_new_server_thread ();
    if (handle_single_connection (0, 1) == -1)
      exit (EXIT_FAILURE);
    return;
  }

  /* Handling multiple connections on TCP/IP or a Unix domain socket. */
  if (unixsocket)
    socks = bind_unix_socket (&nr_socks);
  else
    socks = bind_tcpip_socket (&nr_socks);

  run_command ();
  change_user ();
  fork_into_background ();
  write_pidfile ();
  accept_incoming_connections (socks, nr_socks);
  free_listening_sockets (socks, nr_socks);
}

static void
write_pidfile (void)
{
  int fd;
  pid_t pid;
  char pidstr[64];
  size_t len;

  if (!pidfile)
    return;

  pid = getpid ();
  snprintf (pidstr, sizeof pidstr, "%d\n", pid);
  len = strlen (pidstr);

  fd = open (pidfile, O_WRONLY|O_TRUNC|O_CREAT|O_CLOEXEC|O_NOCTTY, 0644);
  if (fd == -1) {
    perror (pidfile);
    exit (EXIT_FAILURE);
  }

  if (write (fd, pidstr, len) < len ||
      close (fd) == -1) {
    perror (pidfile);
    exit (EXIT_FAILURE);
  }

  debug ("written pidfile %s", pidfile);
}

/* When parsing plugin and filter config key=value from the command
 * line, check that the key is a simple alphanumeric with period,
 * underscore or dash.
 *
 * Note this doesn't return an error.  If the key is not valid then we
 * return false and the parsing code will assume that this is a bare
 * value instead.
 */
static int
is_config_key (const char *key, size_t len)
{
  static const char allowed_first[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const char allowed[] =
    "._-"
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (len == 0)
    return 0;

  if (strchr (allowed_first, key[0]) == NULL)
    return 0;

  /* This works in context of the caller since key[len] == '='. */
  if (strspn (key, allowed) != len)
    return 0;

  return 1;
}
