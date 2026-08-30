/* Test plugin.
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

// Used in tests/test-golang-extents.sh

package main

import (
	"C"
	"libguestfs.org/nbdkit"
	"unsafe"
)

var pluginName = "extents"

type ExtentsPlugin struct {
	nbdkit.Plugin
}

type ExtentsConnection struct {
	nbdkit.Connection
}

var (
	size    uint64 = 1024 * 1024
	extents        = []nbdkit.Extent{
		{0, 1000, 0},
		{1000, 1000, nbdkit.ExtentHole | nbdkit.ExtentZero},
		{2000, 1, 0},
		{2001, 999, nbdkit.ExtentHole | nbdkit.ExtentZero},
		{3000, 1, 0},
		{3001, 1, nbdkit.ExtentHole | nbdkit.ExtentZero},
		{3002, 1, 0},
		{3003, 1, nbdkit.ExtentHole | nbdkit.ExtentZero},
		{3004, size - 3004, 0},
	}
)

func (p *ExtentsPlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	return &ExtentsConnection{}, nil
}

func (c *ExtentsConnection) GetSize() (uint64, error) {
	return size, nil
}

func (c *ExtentsConnection) CanMultiConn() (bool, error) {
	// Multi-conn is trivially safe with this plugin because the
	// plugin is read-only.
	return true, nil
}

func (c *ExtentsConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	// XXX Return data in the non-extents sections.
	for i := 0; i < len(buf); i++ {
		buf[i] = 0
	}
	return nil
}

func (c *ExtentsConnection) CanExtents() (bool, error) {
	return true, nil
}

func (c *ExtentsConnection) Extents(count uint32, offset uint64,
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
	return nbdkit.PluginInitialize(pluginName, &ExtentsPlugin{})
}

// This is never(?) called, but must exist.
func main() {}
