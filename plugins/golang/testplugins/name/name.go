/* Test plugin for block size.
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

// This is used by tests/test-golang-name.sh

package main

import (
	"C"
	"libguestfs.org/nbdkit"
	"unsafe"
)

var pluginName = "name"

type NamePlugin struct {
	nbdkit.Plugin
}

type NameConnection struct {
	nbdkit.Connection
}

func (p *NamePlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	return &NameConnection{}, nil
}

func (c *NameConnection) GetSize() (uint64, error) {
	name, _ := nbdkit.Name()
	return uint64(len(name)), nil
}

func (c *NameConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	name, _ := nbdkit.Name()
	copy(buf, name[offset:int(offset)+len(buf)])
	return nil
}

//----------------------------------------------------------------------
//
// The boilerplate below this line is required by all golang plugins,
// as well as importing "C" and "unsafe" modules at the top of the
// file.

//export plugin_init
func plugin_init() unsafe.Pointer {
	// If your plugin needs to do any initialization, you can
	// either put it here or implement a Load() method.
	// ...

	// Then you must call the following function.
	return nbdkit.PluginInitialize(pluginName, &NamePlugin{})
}

// This is never(?) called, but must exist.
func main() {}
