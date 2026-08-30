/* Go helper functions.
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
#cgo !darwin LDFLAGS: -Wl,--unresolved-symbols=ignore-in-object-files
#cgo darwin LDFLAGS: -Wl,-undefined,dynamic_lookup

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>
#include "wrappers.h"
*/
import "C"

import (
	"fmt"
	"reflect"
	"sync"
	"syscall"
	"unsafe"
)

// The plugin may raise errors by returning this struct (instead of nil).
type PluginError struct {
	Errmsg string        // string (passed to nbdkit_error)
	Errno  syscall.Errno // errno (optional, use 0 if not available)
}

func (e PluginError) String() string {
	if e.Errno != 0 {
		return e.Errmsg
	} else {
		return fmt.Sprintf("%s (errno %d)", e.Errmsg, e.Errno)
	}
}

func (e PluginError) Error() string {
	return e.String()
}

// Flags and other constants.
const (
	ThreadModelSerializeConnections = uint32(C.NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS)
	ThreadModelSerializeAllRequests = uint32(C.NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS)
	ThreadModelSerializeRequests    = uint32(C.NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS)
	ThreadModelParallel             = uint32(C.NBDKIT_THREAD_MODEL_PARALLEL)

	FlagMayTrim  = uint32(C.NBDKIT_FLAG_MAY_TRIM)
	FlagFUA      = uint32(C.NBDKIT_FLAG_FUA)
	FlagReqOne   = uint32(C.NBDKIT_FLAG_REQ_ONE)
	FlagFastZero = uint32(C.NBDKIT_FLAG_FAST_ZERO)

	FUANone    = uint32(C.NBDKIT_FUA_NONE)
	FUAEmulate = uint32(C.NBDKIT_FUA_EMULATE)
	FUANative  = uint32(C.NBDKIT_FUA_NATIVE)

	CacheNone    = uint32(C.NBDKIT_CACHE_NONE)
	CacheEmulate = uint32(C.NBDKIT_CACHE_EMULATE)
	CacheNative  = uint32(C.NBDKIT_CACHE_NATIVE)

	ExtentHole = uint32(C.NBDKIT_EXTENT_HOLE)
	ExtentZero = uint32(C.NBDKIT_EXTENT_ZERO)

	// This is not defined by the header, but by this file.  It
	// might be useful for plugins to know this (even though they
	// probably wouldn't be source-compatible with other API
	// versions) so we expose it to golang code.
	APIVersion = uint32(C.NBDKIT_API_VERSION)
)

// List of exports returned by ListExports()
type Export struct {
	Name        string
	Description string
}

// List of extents returned by Extents()
type Extent struct {
	Offset uint64
	Length uint64
	Type   uint32
}

// The plugin interface.
type PluginInterface interface {
	Load()
	Unload()

	DumpPlugin()

	Version() string
	LongName() string
	Description() string
	MagicConfigKey() string

	Config(key string, value string) error
	ConfigComplete() error
	ConfigHelp() string

	DefaultExport(readonly bool, is_tls bool) (string, error)
	ListExports(readonly bool, is_tls bool) ([]Export, error)

	GetReady() error

	PreConnect(readonly bool) error
	Open(readonly bool) (ConnectionInterface, error) // required
}

// The client connection interface.
type ConnectionInterface interface {
	GetSize() (uint64, error) // required
	BlockSize() (uint32, uint32, uint32, error)
	IsRotational() (bool, error)
	CanMultiConn() (bool, error)
	CanFUA() (uint32, error)
	ExportDescription() (string, error)

	PRead(buf []byte, offset uint64, flags uint32) error // required

	// NB: PWrite will NOT be called unless CanWrite returns true.
	CanWrite() (bool, error)
	PWrite(buf []byte, offset uint64, flags uint32) error

	// NB: Flush will NOT be called unless CanFlush returns true.
	CanFlush() (bool, error)
	Flush(flags uint32) error

	// NB: Trim will NOT be called unless CanTrim returns true.
	CanTrim() (bool, error)
	Trim(count uint32, offset uint64, flags uint32) error

	// NB: Zero will NOT be called unless CanZero returns true.
	CanZero() (bool, error)
	Zero(count uint32, offset uint64, flags uint32) error

	// NB: Extents will NOT be called unless CanExtents returns true.
	CanExtents() (bool, error)
	Extents(count uint32, offset uint64, flags uint32) ([]Extent, error)

	// NB: Cache will NOT be called unless CanCache returns true.
	CanCache() (bool, error)
	Cache(count uint32, offset uint64, flags uint32) error

	Close()
}

// Default implementations for plugin interface methods.
type Plugin struct{}
type Connection struct{}

func (p *Plugin) Load() {
}

func (p *Plugin) Unload() {
}

func (p *Plugin) DumpPlugin() {
}

func (p *Plugin) Version() string {
	return ""
}

func (p *Plugin) LongName() string {
	return ""
}

func (p *Plugin) Description() string {
	return ""
}

func (p *Plugin) MagicConfigKey() string {
	return ""
}

func (p *Plugin) Config(key string, value string) error {
	return nil
}

func (p *Plugin) ConfigComplete() error {
	return nil
}

func (p *Plugin) DefaultExport(readonly bool, is_tls bool) (string, error) {
	return "", nil
}

func (p *Plugin) ListExports(readonly bool, is_tls bool) ([]Export, error) {
	return nil, nil
}

func (p *Plugin) ConfigHelp() string {
	return ""
}

func (p *Plugin) GetReady() error {
	return nil
}

func (p *Plugin) PreConnect(readonly bool) error {
	return nil
}

func (p *Plugin) Open(readonly bool) (ConnectionInterface, error) {
	panic("plugin must implement Open()")
}

func (c *Connection) Close() {
}

func (c *Connection) GetSize() (uint64, error) {
	panic("plugin must implement GetSize()")
}

func (c *Connection) BlockSize() (uint32, uint32, uint32, error) {
	return 1, 4096, 0xffffffff, nil
}

func (c *Connection) CanWrite() (bool, error) {
	return false, nil
}

func (c *Connection) CanFlush() (bool, error) {
	return false, nil
}

func (c *Connection) IsRotational() (bool, error) {
	return false, nil
}

func (c *Connection) CanTrim() (bool, error) {
	return false, nil
}

func (c *Connection) CanZero() (bool, error) {
	return false, nil
}

func (c *Connection) CanExtents() (bool, error) {
	return false, nil
}

func (c *Connection) CanCache() (bool, error) {
	return false, nil
}

func (c *Connection) CanMultiConn() (bool, error) {
	return false, nil
}

func (c *Connection) CanFUA() (uint32, error) {
	// XXX This is not a good default.  Ideally we would test if
	// Flush is defined or not.
	return FUANone, nil
}

func (c *Connection) ExportDescription() (string, error) {
	return "", nil
}

func (c *Connection) PRead(buf []byte, offset uint64, flags uint32) error {
	panic("plugin must implement PRead()")
}

func (c *Connection) PWrite(buf []byte, offset uint64, flags uint32) error {
	panic("plugin CanWrite returns true, but no PWrite() function")
}

func (c *Connection) Flush(flags uint32) error {
	// nbdkit can in fact call Flush even if CanFlush exists and
	// returns false.  This is hinted in nbdkit-plugin(3) but not
	// very clearly.  If CanFUA returns FUAEmulate, PWrite + FUA
	// calls Flush, ignoring CanFlush.  If Flush was not defined
	// then we end up here.
	//
	// The best we can do is ignore the "unexpected" Flush.
	//
	// We don't return an error because that would cause PWrite +
	// FUA to fail, and we assume if the plugin didn't implement
	// Flush then it's not needed.
	return nil
}

func (c *Connection) Trim(count uint32, offset uint64, flags uint32) error {
	panic("plugin CanTrim returns true, but no Trim() function")
}

func (c *Connection) Zero(count uint32, offset uint64, flags uint32) error {
	panic("plugin CanZero returns true, but no Zero() function")
}

func (c *Connection) Extents(count uint32, offset uint64, flags uint32) ([]Extent, error) {
	panic("plugin CanExtents returns true, but no Extents() function")
}

func (c *Connection) Cache(count uint32, offset uint64, flags uint32) error {
	panic("plugin CanCache returns true, but no Cache() function")
}

// The implementation of the user plugin.
var pluginImpl PluginInterface
var nextConnectionId uintptr
var connectionMap map[uintptr]ConnectionInterface
var connectionMutex sync.RWMutex

// Callbacks from the server.  These translate C to Go and back.

func set_error(err error) {
	perr, ok := err.(PluginError)
	if ok {
		if perr.Errno != 0 {
			SetError(perr.Errno)
		}
		Error(perr.Errmsg)
	} else {
		Error(err.Error())
	}
}

//export implLoad
func implLoad() {
	pluginImpl.Load()
}

//export implUnload
func implUnload() {
	pluginImpl.Unload()
}

//export implDumpPlugin
func implDumpPlugin() {
	pluginImpl.DumpPlugin()
}

//export implConfig
func implConfig(key *C.char, value *C.char) C.int {
	err := pluginImpl.Config(C.GoString(key), C.GoString(value))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implConfigComplete
func implConfigComplete() C.int {
	err := pluginImpl.ConfigComplete()
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implDefaultExport
func implDefaultExport(creadonly C.int, cis_tls C.int) *C.char {
	readonly := false
	if creadonly != 0 {
		readonly = true
	}
	is_tls := false
	if cis_tls != 0 {
		is_tls = true
	}
	s, err := pluginImpl.DefaultExport(readonly, is_tls)
	if err != nil {
		set_error(err)
		return nil
	}
	// Avoid leaking the string by interning it.
	cs := C.CString(s)
	ccs := C.nbdkit_strdup_intern(cs)
	C.free(unsafe.Pointer(cs))
	return ccs
}

//export implListExports
func implListExports(creadonly C.int, cis_tls C.int,
	exports *C.struct_nbdkit_exports) C.int {
	readonly := false
	if creadonly != 0 {
		readonly = true
	}
	is_tls := false
	if cis_tls != 0 {
		is_tls = true
	}
	elist, err := pluginImpl.ListExports(readonly, is_tls)
	if err != nil {
		set_error(err)
		return -1
	}
	for _, e := range elist {
		name := C.CString(e.Name)
		defer C.free(unsafe.Pointer(name))
		var description *C.char
		if e.Description != "" {
			description = C.CString(e.Description)
		}
		defer C.free(unsafe.Pointer(description))
		if C.nbdkit_add_export(exports, name, description) == -1 {
			return -1
		}
	}
	return 0
}

//export implGetReady
func implGetReady() C.int {
	err := pluginImpl.GetReady()
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implPreConnect
func implPreConnect(c_readonly C.int) C.int {
	readonly := false
	if c_readonly != 0 {
		readonly = true
	}
	err := pluginImpl.PreConnect(readonly)
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implOpen
func implOpen(c_readonly C.int) unsafe.Pointer {
	readonly := false
	if c_readonly != 0 {
		readonly = true
	}
	h, err := pluginImpl.Open(readonly)
	if err != nil {
		set_error(err)
		return nil
	}
	connectionMutex.Lock()
	id := nextConnectionId
	nextConnectionId++
	connectionMap[id] = h
	connectionMutex.Unlock()
	return unsafe.Pointer(id)
}

func getConn(handle unsafe.Pointer) ConnectionInterface {
	id := uintptr(handle)
	connectionMutex.RLock()
	h, ok := connectionMap[id]
	connectionMutex.RUnlock()
	if !ok {
		panic(fmt.Sprintf("connection %d was not open", id))
	}
	return h
}

//export implClose
func implClose(handle unsafe.Pointer) {
	h := getConn(handle)
	h.Close()
	id := uintptr(handle)
	connectionMutex.Lock()
	delete(connectionMap, id)
	connectionMutex.Unlock()
}

//export implGetSize
func implGetSize(handle unsafe.Pointer) C.int64_t {
	h := getConn(handle)
	size, err := h.GetSize()
	if err != nil {
		set_error(err)
		return -1
	}
	return C.int64_t(size)
}

//export implBlockSize
func implBlockSize(handle unsafe.Pointer, cminimum *C.uint32_t,
	cpreferred *C.uint32_t, cmaximum *C.uint32_t) C.int {
	h := getConn(handle)
	minimum, preferred, maximum, err := h.BlockSize()
	if err != nil {
		set_error(err)
		return -1
	}
	*cminimum = C.uint32_t(minimum)
	*cpreferred = C.uint32_t(preferred)
	*cmaximum = C.uint32_t(maximum)
	return 0
}

//export implCanWrite
func implCanWrite(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanWrite()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanFlush
func implCanFlush(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanFlush()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implIsRotational
func implIsRotational(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.IsRotational()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanTrim
func implCanTrim(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanTrim()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanZero
func implCanZero(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanZero()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanExtents
func implCanExtents(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanExtents()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanCache
func implCanCache(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanCache()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanMultiConn
func implCanMultiConn(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	b, err := h.CanMultiConn()
	if err != nil {
		set_error(err)
		return -1
	}
	if b {
		return 1
	} else {
		return 0
	}
}

//export implCanFUA
func implCanFUA(handle unsafe.Pointer) C.int {
	h := getConn(handle)
	fuaMode, err := h.CanFUA()
	if err != nil {
		set_error(err)
		return -1
	}
	return C.int(fuaMode)
}

//export implExportDescription
func implExportDescription(handle unsafe.Pointer) *C.char {
	h := getConn(handle)
	s, err := h.ExportDescription()
	// XXX We probably shouldn't allow errors here.  See the
	// description in nbdkit-plugin(3).
	if err != nil {
		set_error(err)
		return nil
	}
	// Avoid leaking the string by interning it.
	cs := C.CString(s)
	ccs := C.nbdkit_strdup_intern(cs)
	C.free(unsafe.Pointer(cs))
	return ccs
}

//export implPRead
func implPRead(handle unsafe.Pointer, c_buf unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t) C.int {
	h := getConn(handle)
	// https://github.com/golang/go/issues/13656
	// https://stackoverflow.com/a/25776046
	hdr := reflect.SliceHeader{
		Data: uintptr(c_buf),
		Len:  int(count),
		Cap:  int(count),
	}
	buf := *(*[]byte)(unsafe.Pointer(&hdr))
	err := h.PRead(buf, uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implPWrite
func implPWrite(handle unsafe.Pointer, c_buf unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t) C.int {
	h := getConn(handle)
	// https://github.com/golang/go/issues/13656
	// https://stackoverflow.com/a/25776046
	hdr := reflect.SliceHeader{
		Data: uintptr(c_buf),
		Len:  int(count),
		Cap:  int(count),
	}
	buf := *(*[]byte)(unsafe.Pointer(&hdr))
	err := h.PWrite(buf, uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implFlush
func implFlush(handle unsafe.Pointer, flags C.uint32_t) C.int {
	h := getConn(handle)
	err := h.Flush(uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implTrim
func implTrim(handle unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t) C.int {
	h := getConn(handle)
	err := h.Trim(uint32(count), uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implZero
func implZero(handle unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t) C.int {
	h := getConn(handle)
	err := h.Zero(uint32(count), uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

//export implExtents
func implExtents(handle unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t,
	extents *C.struct_nbdkit_extents) C.int {
	h := getConn(handle)
	elist, err := h.Extents(uint32(count), uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	for _, e := range elist {
		if C.nbdkit_add_extent(extents,
			C.uint64_t(e.Offset),
			C.uint64_t(e.Length),
			C.uint32_t(e.Type)) == -1 {
			return -1
		}
	}
	return 0
}

//export implCache
func implCache(handle unsafe.Pointer,
	count C.uint32_t, offset C.uint64_t, flags C.uint32_t) C.int {
	h := getConn(handle)
	err := h.Cache(uint32(count), uint64(offset), uint32(flags))
	if err != nil {
		set_error(err)
		return -1
	}
	return 0
}

// Called from C plugin_init function.
func PluginInitialize(name string, impl PluginInterface) unsafe.Pointer {
	// Initialize the connection map.  Note that connection IDs
	// must start counting from 1 since we must never return what
	// looks like a NULL pointer to the C code.
	connectionMap = make(map[uintptr]ConnectionInterface)
	nextConnectionId = 1

	pluginImpl = impl

	plugin := C.struct_nbdkit_plugin{}

	// Set up the hidden plugin fields as for C.
	struct_size := C.ulong(unsafe.Sizeof(plugin))
	plugin._struct_size = C.uint64_t(struct_size)
	plugin._api_version = C.NBDKIT_API_VERSION
	plugin._thread_model = C.NBDKIT_THREAD_MODEL_PARALLEL

	// Set up required name field.
	plugin.name = C.CString(name)

	// Set up optional string fields.
	s := impl.Version()
	if s != "" {
		plugin.version = C.CString(s)
	}
	s = impl.LongName()
	if s != "" {
		plugin.longname = C.CString(s)
	}
	s = impl.Description()
	if s != "" {
		plugin.description = C.CString(s)
	}
	s = impl.MagicConfigKey()
	if s != "" {
		plugin.magic_config_key = C.CString(s)
	}
	s = impl.ConfigHelp()
	if s != "" {
		plugin.config_help = C.CString(s)
	}
	// Set up callback wrappers.
	plugin.load = (*[0]byte)(C.wrapper_load)
	plugin.unload = (*[0]byte)(C.wrapper_unload)
	plugin.dump_plugin = (*[0]byte)(C.wrapper_dump_plugin)
	plugin.config = (*[0]byte)(C.wrapper_config)
	plugin.config_complete = (*[0]byte)(C.wrapper_config_complete)
	plugin.default_export = (*[0]byte)(C.wrapper_default_export)
	plugin.list_exports = (*[0]byte)(C.wrapper_list_exports)
	plugin.get_ready = (*[0]byte)(C.wrapper_get_ready)
	plugin.after_fork = (*[0]byte)(C.nonwrapper_after_fork)
	plugin.preconnect = (*[0]byte)(C.wrapper_preconnect)
	plugin.open = (*[0]byte)(C.wrapper_open)
	plugin.close = (*[0]byte)(C.wrapper_close)
	plugin.get_size = (*[0]byte)(C.wrapper_get_size)
	plugin.block_size = (*[0]byte)(C.wrapper_block_size)
	plugin.can_write = (*[0]byte)(C.wrapper_can_write)
	plugin.can_flush = (*[0]byte)(C.wrapper_can_flush)
	plugin.is_rotational = (*[0]byte)(C.wrapper_is_rotational)
	plugin.can_trim = (*[0]byte)(C.wrapper_can_trim)
	plugin.can_zero = (*[0]byte)(C.wrapper_can_zero)
	plugin.can_extents = (*[0]byte)(C.wrapper_can_extents)
	plugin.can_cache = (*[0]byte)(C.wrapper_can_cache)
	plugin.can_multi_conn = (*[0]byte)(C.wrapper_can_multi_conn)
	plugin.can_fua = (*[0]byte)(C.wrapper_can_fua)
	plugin.export_description = (*[0]byte)(C.wrapper_export_description)
	plugin.pread = (*[0]byte)(C.wrapper_pread)
	plugin.pwrite = (*[0]byte)(C.wrapper_pwrite)
	plugin.flush = (*[0]byte)(C.wrapper_flush)
	plugin.trim = (*[0]byte)(C.wrapper_trim)
	plugin.zero = (*[0]byte)(C.wrapper_zero)
	plugin.extents = (*[0]byte)(C.wrapper_extents)
	plugin.cache = (*[0]byte)(C.wrapper_cache)

	// Golang plugins don't preserve errno correctly.
	plugin.errno_is_preserved = 0

	// Return a newly malloced copy of the struct.  This must be
	// globally available to the C code in the server, so it is
	// never freed.
	p := (*C.struct_nbdkit_plugin)(C.malloc(C.size_t(struct_size)))
	*p = plugin
	return unsafe.Pointer(p)
}
