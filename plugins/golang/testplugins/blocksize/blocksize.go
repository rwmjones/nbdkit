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

// This is used by tests/test-golang-block-size.sh

package main

import (
	"C"
	"libguestfs.org/nbdkit"
	"strconv"
	"unsafe"
)

var pluginName = "blocksize"

type BlocksizePlugin struct {
	nbdkit.Plugin
}

type BlocksizeConnection struct {
	nbdkit.Connection
}

var (
	minimum   uint32 = 0
	preferred uint32 = 0
	maximum   uint32 = 0
)

func (p *BlocksizePlugin) Config(key string, value string) error {
	var err error
	var i uint64
	if key == "minimum" {
		i, err = strconv.ParseUint(value, 10, 32)
		minimum = uint32(i)
	} else if key == "preferred" {
		i, err = strconv.ParseUint(value, 10, 32)
		preferred = uint32(i)
	} else if key == "maximum" {
		i, err = strconv.ParseUint(value, 10, 32)
		maximum = uint32(i)
	} else {
		err = nbdkit.PluginError{"unknown parameter " + key, 0}
	}
	return err
}

func (p *BlocksizePlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	return &BlocksizeConnection{}, nil
}

func (c *BlocksizeConnection) GetSize() (uint64, error) {
	return 0, nil
}

func (c *BlocksizeConnection) BlockSize() (uint32, uint32, uint32, error) {
	return minimum, preferred, maximum, nil
}

func (c *BlocksizeConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	// nothing
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
	return nbdkit.PluginInitialize(pluginName, &BlocksizePlugin{})
}

// This is never(?) called, but must exist.
func main() {}
