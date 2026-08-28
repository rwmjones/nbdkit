/* cgo wrappers.
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

package nbdkit

/*
#cgo pkg-config: nbdkit

#include <stdlib.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

// cgo cannot call varargs functions.
void
_nbdkit_debug (const char *s)
{
  nbdkit_debug ("%s", s);
}

// cgo cannot call varargs functions.
void
_nbdkit_error (const char *s)
{
  nbdkit_error ("%s", s);
}
*/
import "C"

import (
	"syscall"
	"unsafe"
)

// Utility functions.

func Debug(s string) {
	C._nbdkit_debug(C.CString(s))
}

// This function is provided but plugins would rarely need to call
// this explicitly since returning an error from a plugin callback
// will call it implicitly.
func Error(s string) {
	C._nbdkit_error(C.CString(s))
}

// Same applies as for Error().  Callers should not usually need to
// call this.
func SetError(err syscall.Errno) {
	C.nbdkit_set_error(C.int(err))
}

// ReadPassword wraps nbdkit_read_password (literal, +FILE, or -FD).
func ReadPassword(value string) (string, error) {
	cvalue := C.CString(value)
	defer C.free(unsafe.Pointer(cvalue))
	var cpass *C.char
	if C.nbdkit_read_password(cvalue, &cpass) == -1 {
		return "", PluginError{Errmsg: "unable to parse parameter as a password"}
	}
	defer C.free(unsafe.Pointer(cpass))
	return C.GoString(cpass), nil
}

// ParseBool wraps nbdkit_parse_bool (true/false/on/off/yes/no/1/0).
func ParseBool(value string) (bool, error) {
	cvalue := C.CString(value)
	defer C.free(unsafe.Pointer(cvalue))
	r := C.nbdkit_parse_bool(cvalue)
	if r == -1 {
		return false, PluginError{Errmsg: "unable to parse string as boolean"}
	}
	return r != 0, nil
}
