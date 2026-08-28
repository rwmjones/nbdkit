/* Example plugin.
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

// Example random sparse plugin.
//
// This is used in tests/test-golang-*.sh

package main

import (
	"C"
	"libguestfs.org/nbdkit"
	"math/rand"
	"unsafe"
)

var pluginName = "sparse"

type SparsePlugin struct {
	nbdkit.Plugin
}

type SparseConnection struct {
	nbdkit.Connection
}

var (
	size    uint64 = 1024 * 1024 * 1024
	extents []nbdkit.Extent
)

func (p *SparsePlugin) GetReady() error {
	// Initialize random extents
	hole := false
	for offset := uint64(0); offset < size; {
		len := uint64(rand.Intn(1000000) + 1)
		t := uint32(0)
		if hole {
			t = nbdkit.ExtentHole + nbdkit.ExtentZero
		}
		extents = append(extents, nbdkit.Extent{offset, len, t})
		offset += len
		hole = !hole
	}
	return nil
}

func (p *SparsePlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	return &SparseConnection{}, nil
}

func (c *SparseConnection) GetSize() (uint64, error) {
	return size, nil
}

func (c *SparseConnection) CanMultiConn() (bool, error) {
	// Multi-conn is trivially safe with this plugin because the
	// plugin is read-only.
	return true, nil
}

func (c *SparseConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	// XXX Return data in the non-sparse sections.
	for i := 0; i < len(buf); i++ {
		buf[i] = 0
	}
	return nil
}

func (c *SparseConnection) CanExtents() (bool, error) {
	return true, nil
}

func (c *SparseConnection) Extents(count uint32, offset uint64,
	flags uint32) ([]nbdkit.Extent, error) {
	// You can return the whole array ignoring count, offset and
	// nbdkit will do the right thing.
	return extents, nil
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
	return nbdkit.PluginInitialize(pluginName, &SparsePlugin{})
}

// This is never(?) called, but must exist.
func main() {}
