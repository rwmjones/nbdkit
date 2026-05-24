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

/* Functions and constants in the virtual nbdkit.* module. */

/* Python is broken, this MUST come first. */
#define PY_SSIZE_T_CLEAN 1
#include <Python.h>

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "plugin.h"

__thread int last_error;

/* nbdkit.debug */
static PyObject *
debug (PyObject *self, PyObject *args)
{
  const char *msg;

  if (!PyArg_ParseTuple (args, "s:debug", &msg))
    return NULL;
  nbdkit_debug ("%s", msg);
  Py_RETURN_NONE;
}

/* nbdkit.debug_hexdiff */
static PyObject *
do_debug_hexdiff (PyObject *self, PyObject *args)
{
  Py_buffer buf1 = { 0 };
  Py_buffer buf2 = { 0 };
  const char *prefix;
  unsigned long long start;

  if (!PyArg_ParseTuple (args, "y*y*zK:debug_hexdiff",
                         &buf1, &buf2, &prefix, &start))
    return NULL;
  if (buf1.len != buf2.len) {
    PyErr_SetString (PyExc_RuntimeError,
                     "nbdkit.debug_hexdiff: buffers must have equal length");
    return NULL;
  }
  nbdkit_debug_hexdiff (buf1.buf, buf2.buf, buf1.len,
                        prefix, (uint64_t) start);
  PyBuffer_Release (&buf2);
  PyBuffer_Release (&buf1);
  Py_RETURN_NONE;
}

/* nbdkit.debug_hexdump */
static PyObject *
do_debug_hexdump (PyObject *self, PyObject *args)
{
  Py_buffer buf = { 0 };
  const char *prefix;
  unsigned long long start;

  if (!PyArg_ParseTuple (args, "y*zK:debug_hexdump", &buf, &prefix, &start))
    return NULL;
  nbdkit_debug_hexdump (buf.buf, buf.len, prefix, (uint64_t) start);
  PyBuffer_Release (&buf);
  Py_RETURN_NONE;
}

/* nbdkit.export_name */
static PyObject *
export_name (PyObject *self, PyObject *args)
{
  const char *s = nbdkit_export_name ();

  if (!s) {
    /* Unfortunately we lose the actual error. XXX */
    PyErr_SetString (PyExc_RuntimeError, "nbdkit.export_name failed");
    return NULL;
  }

  /* NBD spec says that the export name should be UTF-8, so this
   * ought to work, and if it fails the client gave us a bad export
   * name which should turn into an exception.
   */
  return PyUnicode_FromString (s);
}

/* nbdkit.set_error */
static PyObject *
set_error (PyObject *self, PyObject *args)
{
  int err;

  if (!PyArg_ParseTuple (args, "i:set_error", &err))
    return NULL;
  nbdkit_set_error (err);
  last_error = err;
  Py_RETURN_NONE;
}

/* nbdkit.shutdown */
static PyObject *
do_shutdown (PyObject *self, PyObject *args)
{
  nbdkit_shutdown ();
  Py_RETURN_NONE;
}

/* nbdkit.disconnect */
static PyObject *
do_disconnect (PyObject *self, PyObject *args)
{
  int force;

  if (!PyArg_ParseTuple (args, "p:disconnect", &force))
    return NULL;
  nbdkit_disconnect (force);
  Py_RETURN_NONE;
}

/* nbdkit.parse_size */
static PyObject *
parse_size (PyObject *self, PyObject *args)
{
  const char *s;
  if (!PyArg_ParseTuple (args, "s:parse_size", &s))
    return NULL;

  int64_t size = nbdkit_parse_size (s);
  if (size == -1) {
    PyErr_SetString (PyExc_ValueError, "Unable to parse string as size");
    return NULL;
  }

  return PyLong_FromSize_t ((size_t)size);
}

/* nbdkit.parse_probability */
static PyObject *
parse_probability (PyObject *self, PyObject *args)
{
  const char *what, *str;
  double d;

  if (!PyArg_ParseTuple (args, "ss:parse_probability", &what, &str))
    return NULL;

  if (nbdkit_parse_probability (what, str, &d) == -1) {
    PyErr_SetString (PyExc_ValueError,
                     "Unable to parse string as probability");
    return NULL;
  }

  return PyFloat_FromDouble (d);
}

/* nbdkit.parse_delay */
static PyObject *
parse_delay (PyObject *self, PyObject *args)
{
  const char *what, *str;
  unsigned sec, nsec;

  if (!PyArg_ParseTuple (args, "ss:parse_delay", &what, &str))
    return NULL;

  if (nbdkit_parse_delay (what, str, &sec, &nsec) == -1) {
    PyErr_SetString (PyExc_ValueError,
                     "Unable to parse string as delay");
    return NULL;
  }

  return Py_BuildValue ("(II)", sec, nsec);
}

/* nbdkit.parse_bool */
static PyObject *
parse_bool (PyObject *self, PyObject *args)
{
  const char *str;
  int r;
  PyObject *ret;

  if (!PyArg_ParseTuple (args, "s:parse_bool", &str))
    return NULL;

  r = nbdkit_parse_bool (str);
  if (r == -1) {
    PyErr_SetString (PyExc_ValueError,
                     "Unable to parse string as boolean");
    return NULL;
  }

  ret = r ? Py_True : Py_False;
  Py_INCREF (ret);
  return ret;
}

/* nbdkit.stdio_safe */
static PyObject *
do_stdio_safe (PyObject *self, PyObject *args)
{
  int b = nbdkit_stdio_safe ();
  if (b) Py_RETURN_TRUE; else Py_RETURN_FALSE;
}

/* nbdkit.is_tls */
static PyObject *
do_is_tls (PyObject *self, PyObject *args)
{
  int b = nbdkit_is_tls ();
  if (b) Py_RETURN_TRUE; else Py_RETURN_FALSE;
}

/* nbdkit.nanosleep */
static PyObject *
do_nanosleep (PyObject *self, PyObject *args)
{
  unsigned secs, nsecs;

  if (!PyArg_ParseTuple (args, "II:nanosleep", &secs, &nsecs))
    return NULL;
  /* This can return an error but in this case it is probably best to
   * ignore it since it most probably indicates that the plugin is
   * exiting.
   */
  nbdkit_nanosleep (secs, nsecs);
  Py_RETURN_NONE;
}

/* nbdkit.peer_name is not easy to implement.  We would need to create
 * a Python socket family structure
 * (https://docs.python.org/3/library/socket.html#socket-families).
 * CPython has a function called makesockaddr which does this, but it
 * is not exported.
 */

static PyObject *
peer_id_common (int64_t id, const char *which)
{
  if (id == -1) {
    PyErr_Format (PyExc_ValueError, "Unable to get client %s", which);
    return NULL;
  }
  return PyLong_FromLongLong (id);
}

/* nbdkit.peer_pid */
static PyObject *
do_peer_pid (PyObject *self, PyObject *args)
{
  int64_t id = nbdkit_peer_pid ();
  return peer_id_common (id, "pid");
}

/* nbdkit.peer_uid */
static PyObject *
do_peer_uid (PyObject *self, PyObject *args)
{
  int64_t id = nbdkit_peer_uid ();
  return peer_id_common (id, "uid");
}

/* nbdkit.peer_gid */
static PyObject *
do_peer_gid (PyObject *self, PyObject *args)
{
  int64_t id = nbdkit_peer_gid ();
  return peer_id_common (id, "gid");
}

/* nbdkit.peer_security_context */
static PyObject *
do_peer_security_context (PyObject *self, PyObject *args)
{
  char *s;
  PyObject *r;

  s = nbdkit_peer_security_context ();
  if (s == NULL) {
    PyErr_SetString (PyExc_ValueError, "Unable to get client security context");
    return NULL;
  }
  r = PyUnicode_FromString (s);
  free (s);
  return r;
}

/* nbdkit.peer_tls_dn */
static PyObject *
do_peer_tls_dn (PyObject *self, PyObject *args)
{
  char *s;
  PyObject *r;

  s = nbdkit_peer_tls_dn ();
  if (s == NULL) {
    PyErr_SetString (PyExc_ValueError, "Unable to get client TLS DN");
    return NULL;
  }
  r = PyUnicode_FromString (s);
  free (s);
  return r;
}

/* nbdkit.peer_tls_issuer_dn */
static PyObject *
do_peer_tls_issuer_dn (PyObject *self, PyObject *args)
{
  char *s;
  PyObject *r;

  s = nbdkit_peer_tls_issuer_dn ();
  if (s == NULL) {
    PyErr_SetString (PyExc_ValueError, "Unable to get issuer TLS DN");
    return NULL;
  }
  r = PyUnicode_FromString (s);
  free (s);
  return r;
}

/* nbdkit.read_password */
static PyObject *
do_read_password (PyObject *self, PyObject *args)
{
  const char *s;
  char *password;
  PyObject *r;

  if (!PyArg_ParseTuple (args, "s:read_password", &s))
    return NULL;

  if (nbdkit_read_password (s, &password) == -1) {
    PyErr_SetString (PyExc_ValueError,
                     "Unable to parse parameter as a password");
    return NULL;
  }

  r = PyBytes_FromString (password);
  free (password);
  return r;
}

/* nbdkit.name */
static PyObject *
do_name (PyObject *self, PyObject *args)
{
  const char *name = nbdkit_name ();

  if (name) {
    /* The --name parameter is most likely to be 7bit ASCII, but if
     * it's in any 8bit encoding it's probably UTF-8, hence:
     */
    return PyUnicode_FromString (name);
  }
  else {
    /* name == NULL is not an error, return None. */
    Py_INCREF (Py_None);
    return Py_None;
  }
}

/* nbdkit.timestamp */
static PyObject *
do_timestamp (PyObject *self, PyObject *args)
{
  const char *ts = nbdkit_timestamp ();

  /* The function never returns NULL. */
  return PyUnicode_FromString (ts);
}

static PyMethodDef NbdkitMethods[] = {
  { "debug", debug, METH_VARARGS,
    "Print a debug message" },
  { "debug_hexdump", do_debug_hexdump, METH_VARARGS,
    "Display buffer in hexdump format" },
  { "debug_hexdiff", do_debug_hexdiff, METH_VARARGS,
    "Display differences between two buffers of equal length" },
  { "disconnect", do_disconnect, METH_VARARGS,
    "Request disconnection from current client" },
  { "export_name", export_name, METH_NOARGS,
    "Return the optional export name negotiated with the client" },
  { "is_tls", do_is_tls, METH_NOARGS,
    "Return True if the client completed TLS authentication" },
  { "name", do_name, METH_NOARGS,
    "Return the name of the nbdkit instance" },
  { "nanosleep", do_nanosleep, METH_VARARGS,
    "Sleep for seconds and nanoseconds" },
  { "parse_bool", parse_bool, METH_VARARGS,
    "Parse human-readable boolean strings" },
  { "parse_delay", parse_delay, METH_VARARGS,
    "Parse delay and sleep strings into (sec, nsec) pair" },
  { "parse_probability", parse_probability, METH_VARARGS,
    "Parse probability strings into floating point number" },
  { "parse_size", parse_size, METH_VARARGS,
    "Parse human-readable size strings into bytes" },
  { "peer_pid", do_peer_pid, METH_NOARGS,
    "Return the client process ID for Unix domain sockets" },
  { "peer_uid", do_peer_uid, METH_NOARGS,
    "Return the client user ID for Unix domain sockets" },
  { "peer_gid", do_peer_gid, METH_NOARGS,
    "Return the client group ID for Unix domain sockets" },
  { "peer_security_context", do_peer_security_context, METH_NOARGS,
    "Return the client security context" },
  { "peer_tls_dn", do_peer_tls_dn, METH_NOARGS,
    "Return the client TLS Distinguished Name" },
  { "peer_tls_issuer_dn", do_peer_tls_issuer_dn, METH_NOARGS,
    "Return the client certificate issuer's TLS Distinguished Name" },
  { "read_password", do_read_password, METH_VARARGS,
    "Read a password from a config parameter" },
  { "set_error", set_error, METH_VARARGS,
    "Store an errno value prior to throwing an exception" },
  { "shutdown", do_shutdown, METH_NOARGS,
    "Request asynchronous shutdown" },
  { "stdio_safe", do_stdio_safe, METH_NOARGS,
    "Return True if it is safe to interact with stdin and stdout" },
  { "timestamp", do_timestamp, METH_NOARGS,
    "Generate a timestamp for log messages" },
  { NULL }
};

static struct PyModuleDef moduledef = {
  PyModuleDef_HEAD_INIT,
  "nbdkit",
  "Module used to access nbdkit server API",
  -1,
  NbdkitMethods,
  NULL,
  NULL,
  NULL,
  NULL
};

PyMODINIT_FUNC
create_nbdkit_module (void)
{
  PyObject *m;

  m = PyModule_Create (&moduledef);
  if (m == NULL) {
    nbdkit_error ("could not create the nbdkit API module");
    exit (EXIT_FAILURE);
  }

  /* Constants corresponding to various flags. */
#define ADD_INT_CONSTANT(name)                                      \
  if (PyModule_AddIntConstant (m, #name, NBDKIT_##name) == -1) {    \
    nbdkit_error ("could not add constant %s to nbdkit API module", \
                  #name);                                           \
    exit (EXIT_FAILURE);                                            \
  }
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_CONNECTIONS);
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_ALL_REQUESTS);
  ADD_INT_CONSTANT (THREAD_MODEL_SERIALIZE_REQUESTS);
  ADD_INT_CONSTANT (THREAD_MODEL_PARALLEL);

  ADD_INT_CONSTANT (FLAG_MAY_TRIM);
  ADD_INT_CONSTANT (FLAG_FUA);
  ADD_INT_CONSTANT (FLAG_REQ_ONE);
  ADD_INT_CONSTANT (FLAG_FAST_ZERO);

  ADD_INT_CONSTANT (FUA_NONE);
  ADD_INT_CONSTANT (FUA_EMULATE);
  ADD_INT_CONSTANT (FUA_NATIVE);

  ADD_INT_CONSTANT (CACHE_NONE);
  ADD_INT_CONSTANT (CACHE_EMULATE);
  ADD_INT_CONSTANT (CACHE_NATIVE);

  ADD_INT_CONSTANT (EXTENT_HOLE);
  ADD_INT_CONSTANT (EXTENT_ZERO);
#undef ADD_INT_CONSTANT

  return m;
}
