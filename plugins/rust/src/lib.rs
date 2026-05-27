// vim: tw=80
// Copyright (C) 2020 Axcient
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
// USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

//! Rust bindings to NBDKit.
//!
//! NBDKit is a toolkit for building Network Block Device servers.
//!
//! <https://gitlab.com/nbdkit/nbdkit>
//!
//! To use nbdkit, build your project as a cdylib.  Define your plugin by
//! implementing [`Server`], and then register it with [`plugin!`].  See 
//! ramdisk.rs in the source code for a complete example.
//!
//! ```no_run
//! # use std::ffi::CStr;
//! # use nbdkit::*;
//! #[derive(Default)]
//! struct MyPlugin {
//!     // ...
//!     # _not_used: i32,
//! }
//! impl Server for MyPlugin {
//!     fn get_size(&self) -> Result<i64> {
//!         // ...
//!         # Ok(0)
//!     }
//!
//!     fn name() -> &'static CStr {
//!         c"my_plugin"
//!     }
//!
//!     fn open(_readonly: bool) -> Result<Box<dyn Server>> {
//!         Ok(Box::new(MyPlugin::default()))
//!     }
//!
//!     fn read_at(&self, buf: &mut [u8], offset: u64) -> Result<()> {
//!         // ...
//!         # Ok(())
//!     }
//! }
//! plugin!(MyPlugin {});
//! # fn main() {}
//! ```
#![cfg_attr(docsrs, feature(doc_cfg))]
#![deny(missing_docs)]

use bitflags::bitflags;
#[cfg(feature = "nix")]
pub use nix::sys::socket::SockaddrLike;
use std::{
    ffi::{CStr, CString},
    error,
    fmt,
    io,
    mem,
    os::raw::{c_char, c_int, c_double, c_void},
    ptr,
    slice,
    sync::Once,
};

/// The error type used by [`Result`].
#[derive(Debug)]
pub struct Error {
    source: Box<dyn error::Error + 'static>,
    errno: i32
}
impl Error {
    fn errno(&self) -> i32 {
        self.errno
    }

    /// Create a new Error with a supplied errno.
    ///
    /// # Examples
    /// ```
    /// # use nbdkit::Error;
    /// let e = Error::new(libc::EINVAL, "Invalid value for option foo");
    /// ```
    pub fn new<E>(errno: i32, error: E) -> Error
        where E: Into<Box<dyn error::Error + 'static>>
    {
        Error {
            source: error.into(),
            errno
        }
    }
}
impl error::Error for Error {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        Some(&*self.source)
    }
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.source.fmt(f)
    }
}
impl From<io::Error> for Error {
    fn from(e: io::Error) -> Error {
        Error {
            errno: e.raw_os_error().unwrap_or(0),
            source: Box::new(e),
        }
    }
}

/// The Result type returned by all [`Server`] callbacks.
pub type Result<T> = std::result::Result<T, Error>;

static mut INITIALIZED: bool = false;

mod unreachable {
    use super::*;
    pub(super) fn config(_k: &str, _v: &str) -> Result<()> { unreachable!() }
    pub(super) fn config_complete() -> Result<()> { unreachable!() }
    pub(super) fn dump_plugin() { unreachable!() }
    pub(super) fn open(_: bool) -> Result<Box<dyn Server>> { unreachable!() }
    pub(super) fn preconnect(_: bool) -> Result<()> { unreachable!() }
    pub(super) fn thread_model() -> Result<ThreadModel> { unreachable!() }
}

static mut AFTER_FORK: fn() -> Result<()> = unreachable::config_complete;
static mut CONFIG: fn(k: &str, v: &str) -> Result<()> = unreachable::config;
static mut CONFIG_COMPLETE: fn() -> Result<()> = unreachable::config_complete;
static mut DUMP_PLUGIN: fn() = unreachable::dump_plugin;
static mut GET_READY: fn() -> Result<()> = unreachable::config_complete;
static mut LOAD: fn() = unreachable::dump_plugin;
static mut OPEN: fn(readonly: bool) -> Result<Box<dyn Server>> = unreachable::open;
static mut PRECONNECT: fn(readonly: bool) -> Result<()> = unreachable::preconnect;
static mut THREAD_MODEL: fn() -> Result<ThreadModel> = unreachable::thread_model;
static mut UNLOAD: fn() = unreachable::dump_plugin;
static INIT: Once = Once::new();

bitflags! {
    /// Flags used by multiple [`Server`] methods
    pub struct Flags: u32 {
        /// This flag is used by the [`Server::zero`] callback.
        ///
        /// Indicates that the plugin may punch a hole instead of writing actual
        /// zeros, but only if subsequent reads from that region will return
        /// zeros.  There is no way to disable this flag, although a plugin that
        /// does not support trim as a way to write zeroes may ignore the flag
        /// without violating expected semantics.
        const MAY_TRIM  = 0b00000001;

        /// This flag represents Forced Unit Access semantics.
        ///
        /// It is used by the [`Server::write_at`], [`Server::zero`], and
        /// [`Server::trim`] callbacks to indicate that the plugin must not
        /// return a result until the action has landed in persistent storage.
        /// This flag will not be sent to the plugin unless `can_fua` is
        /// provided to [`plugin!`] and [`Server::can_fua`] returns
        /// [`FuaFlags::Native`].
        const FUA       = 0b00000010;

        /// Used with [`Server::extents`] to indicate that the client is only
        /// requesting information about a single extent.  The plugin may ignore
        /// this flag, or as an optimization it may return
        /// just a single extent.
        const REQ_ONE   = 0b00000100;

        /// This flag is used by [`Server::zero`].
        ///
        /// If supplied, the plugin must decide up front if the implementation
        /// is likely to be faster than a corresponding [`Server::write_at`]; if
        /// not, then it must immediately fail with `ENOTSUP` or
        /// `EOPNOTSUPP` and preferably without modifying the exported
        /// image. It is acceptable to always fail a fast zero request (as a
        /// fast failure is better than attempting the write only to find out
        /// after the fact that it was not fast after all). Note that on Linux,
        /// support for `ioctl(BLKZEROOUT)` is insufficient for determining
        /// whether a zero request to a block device will be fast (because the
        /// kernel will perform a slow fallback when needed).
        const FAST_ZERO = 0b00001000;
    }
}

/// Return values for [`Server::can_cache`]
#[repr(i32)]
pub enum CacheFlags {
    /// Cache support is not advertised to the client.
    None = 0,
    /// Caching is emulated by the server calling [`Server::read_at`] and
    /// ignoring the results.
    Emulate = 1,
    /// The [`Server::cache`] callback will be used.
    Native = 2,
}

impl From<CacheFlags> for i32 {
    fn from(cf: CacheFlags) -> Self {
        cf as Self
    }
}

/// Return values for [`Server::can_fua`]
#[repr(i32)]
pub enum FuaFlags {
    /// FUA support is not advertised to the client
    None = 0,
    /// The [`Server::flush`] callback must work (even if [`Server::can_flush`]
    /// returns false), and FUA support is emulated by calling `Server::flush`
    /// after any write operation;
    Emulate = 1,
    /// The [`Server::write_at`], [`Server::zero`], and [`Server::trim`]
    /// callbacks (if implemented) must handle the flag [`Flags::FUA`], by not
    /// returning until that action has landed in persistent storage.
    Native = 2,
}

impl From<FuaFlags> for i32 {
    fn from(cf: FuaFlags) -> Self {
        cf as Self
    }
}

/// A plugin's maximum thread safety model
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(i32)]
pub enum ThreadModel {
    /// Only a single handle can be open at any time, and all requests happen
    /// from one thread.
    SerializeConnections = 0,

    /// Multiple handles can be open at the same time, but requests are
    /// serialized so that for the plugin as a whole only one
    /// open/read/write/close (etc) request will be in progress at any time.
    ///
    /// This is a useful setting if the library you are using is not
    /// thread-safe. However performance may not be good.
    SerializeAllRequests = 1,

    /// Multiple handles can be open and multiple data requests can happen in
    /// parallel. However only one request will happen per handle at a time (but
    /// requests on different handles might happen concurrently).
    SerializeRequests = 2,

    /// Multiple handles can be open and multiple data requests can happen in
    /// parallel (even on the same handle). The server may reorder replies,
    /// answering a later request before an earlier one.
    ///
    /// All the libraries you use must be thread-safe and reentrant, and any
    /// code that creates a file descriptor should atomically set `FD_CLOEXEC`
    /// if you do not want it accidentally leaked to another thread's child
    /// process. You may also need to provide mutexes for fields in your
    /// connection handle.
    Parallel = 3,
}

/// Used by [`Server::extents`] to report extents to the client
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ExtentType {
    /// A normal, allocated data extent
    Allocated = 0,
    /// An unallocated extent (hole) which does not read back as zeroes. Note
    /// this should only be used in specialized circumstances such as when
    /// writing a plugin for (or to emulate) certain SCSI drives which do not
    /// guarantee that trimmed blocks read back as zeroes.
    Hole = 1,
    /// An allocated extent which is known to contain only zeroes.
    Zero = 2,
    /// An unallocated extent, a.k.a. a “hole”, which reads back as zeroes. This
    /// is the normal type of hole applicable to most disks.
    HoleZero = 3
}

/// Used by [`Server::extents`] to report extents to the client
#[derive(Debug)]
pub struct ExtentHandle(*mut c_void);

impl ExtentHandle {
    /// Report a single extent spanning `[offset .. offset + len)` back to the
    /// client.
    pub fn add(&mut self, offset: u64, len: u64, et: ExtentType) -> Result<()> {
        let r = unsafe { nbdkit_add_extent(self.0, offset, len, et as u32) };
        match r {
            0 => Ok(()),
            -1 => Err(io::Error::last_os_error().into()),
            x => panic!("Undocumented return value {x} from nbdkit_add_extent")
        }
    }
}

/// All the FFI functions called by C code
mod ffi {
    use super::*;

    pub(super) extern "C" fn after_fork() -> c_int {
        match unsafe { AFTER_FORK() } {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn block_size(h: *mut c_void,
                                        minp: *mut u32,
                                        prefp: *mut u32,
                                        maxp: *mut u32) -> c_int {
        let server = unsafe { downcast(h) };
        match server.block_size() {
            Err(e) => {
                set_error(e);
                -1
            },
            Ok((min, pref, max)) => unsafe {
                *minp = min;
                *prefp = pref;
                *maxp = max;
                0
            }
        }
    }

    macro_rules! can_method {
        ( $meth:ident ) => {
            pub(super) extern "C" fn $meth(h: *mut c_void) -> c_int {
                let server = unsafe { downcast(h) };
                match server.$meth() {
                    Err(e) => {
                        set_error(e);
                        -1
                    },
                    Ok(x) => x.into(),
                }
            }
        }
    }

    macro_rules! trim_like {
        ( $meth:ident) => {
            pub(super) extern "C" fn $meth(h: *mut c_void,
                                           count: u32,
                                           offset: u64,
                                           rawflags: u32) -> c_int
            {
                let server = unsafe { downcast(h) };
                let flags = Flags::from_bits(rawflags)
                    .expect(&format!("Unknown flags value {:#x?}", rawflags));
                match server.$meth(count, offset, flags) {
                    Ok(()) => 0,
                    Err(e) => {
                        set_error(e);
                        -1
                    }
                }
            }
        }
    }

    pub(super) extern "C" fn cache(h: *mut c_void,
                                   count: u32,
                                   offset: u64,
                                   _rawflags: u32) -> c_int
    {
        let server = unsafe { downcast(h) };
        match server.cache(count, offset) {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn close(selfp: *mut c_void) {
        unsafe {
            drop(Box::from_raw(selfp as *mut Box<dyn Server>));
        }
    }

    can_method!(can_cache);
    can_method!(can_extents);
    can_method!(can_flush);
    can_method!(can_fua);
    can_method!(can_fast_zero);
    can_method!(can_multi_conn);
    can_method!(can_write);
    can_method!(can_trim);
    can_method!(can_zero);

    pub(super) extern "C" fn config(k: *const c_char,
                                    v: *const c_char) -> c_int {
        let key = match unsafe { CStr::from_ptr(k) }.to_str() {
            Ok(s) => s,
            Err(e) => {
                let error = Error::new(libc::EINVAL, e.to_string());
                set_error(error);
                return -1;
            }
        };
        let value = match unsafe { CStr::from_ptr(v) }.to_str() {
            Ok(s) => s,
            Err(e) => {
                let error = Error::new(libc::EINVAL, e.to_string());
                set_error(error);
                return -1;
            }
        };
        match unsafe { CONFIG(key, value) } {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn config_complete() -> c_int {
        match unsafe { CONFIG_COMPLETE() } {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    /// Deference the pointer into a trait object reference
    ///
    /// # Safety
    ///
    /// The pointer must be a valid pointer to `Box<Box<dyn Server>>`.  The
    /// pointer's lifetime must be valid for the duration of the returned
    /// reference.
    unsafe fn downcast<'a>(p: *const c_void) -> &'a dyn Server {
        &**(p as *const Box<dyn Server>)
    }

    pub(super) extern "C" fn dump_plugin() {
        unsafe { DUMP_PLUGIN() }
    }

    // TODO: document REQ_ONE
    pub(super) extern "C" fn extents(h: *mut c_void,
                                     count: u32,
                                     offset: u64,
                                     rawflags: u32,
                                     extents: *mut c_void ) -> c_int
    {
        let server = unsafe { downcast(h) };
        let mut exh = ExtentHandle(extents);
        let flags = Flags::from_bits(rawflags).expect("Unknown extents flags");
        match server.extents(count, offset, flags, &mut exh) {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn flush(h: *mut c_void, _rawflags: u32) -> c_int {
        let server = unsafe { downcast(h) };
        match server.flush() {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn get_ready() -> c_int {
        match unsafe { GET_READY() } {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn get_size(h: *mut c_void) -> i64 {
        let server = unsafe { downcast(h) };
        match server.get_size() {
            Ok(size) => size,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    can_method!(is_rotational);

    pub(super) extern "C" fn load() {
        unsafe { LOAD() }
    }

    pub(super) extern "C" fn open(readonly: c_int) -> *mut c_void {
        // We need to double-box to turn the trait object (fat pointer) into a
        // thin pointer
        match unsafe { OPEN(readonly != 0) } {
            Ok(server) =>
                // Leak the memory to C.  We'll get it back in close
                Box::into_raw(Box::new(server)) as *mut c_void,
            Err(e) => {
                set_error(e);
                ptr::null_mut()
            }
        }
    }

    pub(super) extern "C" fn pread(h: *mut c_void,
                                   bufp: *mut c_char,
                                   count: u32,
                                   offset: u64,
                                   _flags: u32) -> c_int
    {
        let server = unsafe { downcast(h) };
        let buf = unsafe {
            slice::from_raw_parts_mut(bufp as *mut u8, count as usize)
        };
        match server.read_at(buf, offset) {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn preconnect(readonly: c_int) -> c_int {
        match unsafe { PRECONNECT(readonly != 0) } {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    pub(super) extern "C" fn pwrite(h: *mut c_void,
                                    bufp: *const c_char,
                                    count: u32,
                                    offset: u64,
                                    rawflags: u32) -> c_int
    {
        let server = unsafe { downcast(h) };
        let buf = unsafe {
            slice::from_raw_parts(bufp as *mut u8, count as usize)
        };
        let flags = Flags::from_bits(rawflags).expect("Unknown pwrite flags");
        match server.write_at(buf, offset, flags) {
            Ok(()) => 0,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    fn set_error(e: Error) {
        let fmt = CString::new("%s").unwrap();
        let msg = CString::new(e.to_string()).expect("CString::new");
        unsafe {
            nbdkit_error(fmt.as_ptr(), msg.as_ptr());
            nbdkit_set_error(e.errno());
        }
    }

    pub(super) extern "C" fn thread_model() -> c_int {
        match unsafe { THREAD_MODEL() } {
            Ok(x) => x as c_int,
            Err(e) => {
                set_error(e);
                -1
            }
        }
    }

    trim_like!(trim);

    pub(super) extern "C" fn unload() {
        unsafe { UNLOAD() }
    }

    trim_like!(zero);
}


/// Define the entry point for your plugin.
///
/// Any of the optional methods may be implemented.  If so, each must be
/// registered with [`plugin!`].  It is an error to register any method that you
/// don't implement.
// We want the argument names to show up without underscores in the API docs
#[allow(unused_variables)]
pub trait Server {
    /// This optional callback is called before the server starts serving.
    ///
    /// It is called after the server forks and changes directory. If
    /// a plugin needs to create background threads it should do so here.
    fn after_fork() -> Result<()> where Self: Sized { unimplemented!() }

    /// Return block size hints to the client.
    ///
    /// This is called during option negotiation to get the
    /// minimum, preferred and maximum block size (all in bytes).
    /// The client should (but is not required to) obey these
    /// constraints by not making requests smaller than the
    /// minimum block size or larger than the maximum size
    /// and usually making requests of a multiple of the
    /// preferred size.  Additionally requests should be
    /// aligned to the minimum (and usually the preferred)
    /// block size.
    fn block_size(&self) -> Result<(u32, u32, u32)> { unimplemented!() }

    /// Indicates that the client intends to make further accesses to the given
    /// data region.
    ///
    /// The nature of caching is not specified further by the NBD specification
    /// (for example, a server may place limits on how much may be cached at
    /// once, and there is no way to control if writes to a cached area have
    /// write-through or write-back semantics). In fact, the cache command can
    /// always fail and still be compliant, and success might not guarantee a
    /// performance gain. If this callback is omitted, then the results of
    /// [`Server::can_cache`] determine whether nbdkit will reject cache
    /// requests, treat them as instant success, or emulate caching by calling
    /// [`Server::pread` over the same region and ignoring the results.
    fn cache(&self, count: u32, offset: u64) -> Result<()> {
        unimplemented!()
    }

    /// Indicate level of caching support to the client.
    ///
    /// This is called during the option negotiation phase to find out if the
    /// plugin supports a cache operation. The nature of the caching is
    /// unspecified (including whether there are limits on how much can be
    /// cached at once, and whether writes to a cached region have write-through
    /// or write-back semantics), but the command exists to let clients issue a
    /// hint to the server that they will be accessing that region of the
    /// export.
    fn can_cache(&self) -> Result<CacheFlags> { unimplemented!() }

    /// Indicate to the client whether the plugin supports detecting allocated
    /// (non-sparse) regions of the disk with the [`Server::extents`]
    fn can_extents(&self) -> Result<bool> { unimplemented!() }

    /// Indicate to the client whether the plugin supports the flush-to-disk
    /// operation.
    fn can_flush(&self) -> Result<bool> { unimplemented!() }

    /// Indicate to the client whether the plugin supports fast-zero requests.
    fn can_fast_zero(&self) -> Result<bool> { unimplemented!() }

    /// Indicate to the client whether the plugin supports Forced Unit Access
    /// (FUA) flag on write, trim, and zero requests.
    ///
    /// If this callback is not implemented, then nbdkit checks whether
    /// [`Server::flush`] is implemented,
    /// exists, and behaves as if this function returns [`FuaFlags::None`] or
    /// [`FuaFlags::Emulate`] as appropriate.
    fn can_fua(&self) -> Result<FuaFlags> { unimplemented!() }

    /// Indicate to the client whether the plugin is prepared to handle multiple
    /// connections from a single client.
    ///
    /// If this method returns `true` then a
    /// client may try to open multiple connections to the nbdkit server and
    /// spread requests across all connections to maximize parallelism. If it
    /// returns `false` false (which is the default) then well-behaved clients
    /// should only open a single connection, although we cannot control what
    /// clients do in practice.
    ///
    /// Specifically it means that either the plugin does not cache requests at
    /// all, or if it does cache them then the effects of a [`Server::flush`]
    /// request or setting [`Flags::FUA`] on a write/trim/zero must be visible
    /// across all connections to the plugin before the plugin replies to that
    /// request.
    fn can_multi_conn(&self) -> Result<bool> { unimplemented!() }

    /// Indicate to the client whether the plugin supports the trim/discard
    /// operation for punching holes in the backing store.
    fn can_trim(&self) -> Result<bool> { unimplemented!() }

    /// Indicates to the client whether the plugin supports writes
    fn can_write(&self) -> Result<bool> { unimplemented!() }

    /// Indicates to the client whether the [`Server::zero`] callback should be
    /// used.
    ///
    /// Support for writing zeroes is still advertised to the client, so
    /// returning false merely serves as a way to avoid complicating the
    /// [`Server::zero`] callback to have to fail with `ENOTSUP` or
    /// `EOPNOTSUPP` on the connections where it will never be more efficient
    /// than using [`Server::write_at`] up front.
    fn can_zero(&self) -> Result<bool> { unimplemented!() }

    /// Supplies command-line parameters, one at a time, to the plugin.
    ///
    /// On the nbdkit command line, after the plugin filename, come an optional
    /// list of key=value arguments. These are passed to the plugin through this
    /// callback when the plugin is first loaded and before any connections are
    /// accepted.
    ///
    /// This callback may be called zero or more times.
    ///
    /// The key will be a non-empty string beginning with an ASCII alphabetic
    /// character (A-Z a-z). The rest of the key must contain only ASCII
    /// alphanumeric plus period, underscore or dash characters (A-Z a-z 0-9 . _
    /// -). The value may be an arbitrary string, including an empty string.
    ///
    /// The names of keys accepted by plugins is up to the plugin, but you
    /// should probably look at other plugins and follow the same conventions.
    fn config(key: &str, value: &str) -> Result<()> where Self: Sized {
        unimplemented!()
    }

    /// This optional callback is called after all the configuration has been
    /// passed to the plugin.
    ///
    /// It is a good place to do checks, for example that the user has passed
    /// the required parameters to the plugin.
    fn config_complete() -> Result<()> where Self: Sized { unimplemented!() }

    /// This optional callback is called when the `nbdkit plugin --dump-plugin`
    /// command is used. It should print any additional informative key=value
    /// fields to stdout as needed. Prefixing the keys with the name of the
    /// plugin will avoid conflicts.
    fn dump_plugin() where Self: Sized { unimplemented!() }

    /// This optional multi-line help message should summarize any key=value
    /// parameters that it takes. It does not need to repeat what already
    /// appears in [`Server::description`].
    fn config_help() -> Option<&'static CStr> where Self: Sized { None }

    /// An optional multi-line description of the plugin.
    fn description() -> Option<&'static CStr> where Self: Sized { None }

    /// During the data serving phase, this callback is used to detect
    /// allocated, sparse and zeroed regions of the disk.
    ///
    /// This function will not be called if [`Server::can_extents`] returned `false`.
    /// nbdkit's default behaviour in this case is to treat the whole virtual
    /// disk as if it were allocated. Also, this function will not be called by
    /// a client that does not request structured replies (the `--no-sr` option
    /// of nbdkit can be used to test behavior when `extents` is unavailable to
    /// the client).
    ///
    /// The callback should detect and return the list of extents overlapping
    /// the range `[offset...offset+count)`.  Each extent should be reported
    /// by calling [`ExtentHandle::add`].
    ///
    /// The flags parameter of the `extents` callback may contain
    /// [`Flags::REQ_ONE`]. This means that the client is only requesting
    /// information about the extent overlapping `offset`. The plugin may ignore
    /// this flag, or as an optimization it may return just a single extent for
    /// offset.
    // Alternatively, the method could be defined as returning a Vec of extent
    // objects, rather than using the ExtentHandle.  I'm not sure which would be
    // more ergonomic, but this way requires fewer memory allocations.
    fn extents(&self,
               count: u32,
               offset: u64,
               flags: Flags,
               extent_handle: &mut ExtentHandle)
        -> Result<()>
    {
        unimplemented!()
    }

    /// During the data serving phase, this callback is used to sync the
    /// backing store, ie. to ensure it has been completely written to a
    /// permanent medium. If that is not possible then you can omit this
    /// callback.
    ///
    /// This function will not be called directly by the client if
    /// [`Server::can_flush`] returned `false`; however, it may still be called
    /// by nbdkit if [`Server::can_fua`] returned [`FuaFlags::Emulate`].
    fn flush(&self) -> Result<()> { unimplemented!() }

    /// This optional callback is called before the server starts serving.
    ///
    /// It is called before the server forks or changes directory. It is the
    /// last chance to do any global preparation that is needed to serve
    /// connections.
    fn get_ready() -> Result<()> where Self: Sized { unimplemented!() }

    /// Return the size in bytes of the exported block device
    fn get_size(&self) -> Result<i64>;

    /// Return `true` if the backing store is a rotational medium (like a
    /// traditional hard disk) as opposed to a non-rotating one like an SSD.
    ///
    /// This may cause the client to reorder requests to make them more
    /// efficient for a slow rotating disk.
    fn is_rotational(&self) -> Result<bool> { unimplemented!() }

    /// This is called once just after the plugin is loaded into memory. You can
    /// use this to perform any global initialization needed by the plugin.
    fn load() where Self: Sized { unimplemented!() }

    /// An optional free text name of the plugin. This field is used in error
    /// messages.
    fn longname() -> Option<&'static CStr> where Self: Sized { None }

    /// This optional string can be used to set a "magic" key used when parsing
    /// plugin parameters. It affects how "bare parameters" (those which do not
    /// contain an = character) are parsed on the command line.
    ///
    /// If `magic_config_key().is_some()` then any bare parameters are passed to
    /// the [`Server::config`] method as: `config (magic_config_key, argv[i]);`.
    ///
    /// If `magic_config_key().is_none()` then we behave as in nbdkit < 1.7: If
    /// the first parameter on the command line is bare then it is passed to the
    /// `Server::config` method as: `config("script", value);`. Any other bare
    /// parameters give errors.
    fn magic_config_key() -> Option<&'static CStr> where Self: Sized { None }

    /// The name of the plugin.
    ///
    /// It must contain only ASCII alphanumeric characters and be unique amongst
    /// all plugins.
    fn name() -> &'static CStr where Self: Sized;

    /// Allocate and return a new `Server` handle to the client.
    ///
    /// Called whenever a new client connects to the server.  The `readonly`
    /// flag informs the plugin that the server was started with the -r flag on
    /// the command line which forces connections to be read-only. Note that the
    /// plugin may *additionally* force the connection to be readonly (even if
    /// this flag is false) by returning false from the [`Server::can_write`]
    /// callback.  So if your plugin can only serve read-only, you can ignore
    /// this parameter.
    fn open(readonly: bool) -> Result<Box<dyn Server>> where Self: Sized;

    /// This optional callback is called when a TCP connection has been made to
    /// the server. This happens early, before NBD or TLS negotiation. If TLS
    /// authentication is required to access the server, then it has not been
    /// negotiated at this point.
    ///
    /// For security reasons (to avoid denial of service attacks) this callback
    /// should be written to be as fast and take as few resources as possible.
    /// If you use this callback, only use it to do basic access control, such
    /// as checking
    #[cfg_attr(feature = "nix", doc = "[`peername`]")]
    #[cfg_attr(not(feature = "nix"), doc = "the peer name")]
    /// against a whitelist.  It may be better to do access control outside the
    /// server, for example using TCP wrappers or a firewall.
    ///
    /// The `readonly` flag informs the plugin that the server was started with
    /// the `-r` flag on the command line.
    ///
    /// Returning `Ok(())` will allow the connection to continue. If there is an
    /// error or you want to deny the connection, return an error.
    fn preconnect(readonly: bool) -> Result<()> where Self: Sized {
        unimplemented!()
    }

    /// Read data from the backing store, starting at `offset`.
    ///
    /// The callback must read the entire range if it can.  If it, it should
    /// return an error.
    fn read_at(&self, buf: &mut [u8], offset: u64) -> Result<()>;

    /// This optional callback is called after all the configuration has been
    /// passed to the plugin.
    ///
    /// It can be used to force a stricter thread model than the default
    /// ([`ThreadModel::Parallel`]).
    fn thread_model() -> Result<ThreadModel> where Self: Sized { unimplemented!() }

    /// Punch a hole in the backing store.
    ///
    /// This function will not be called if [`Server::can_trim`] returned
    /// `false`. The parameter flags may include `Flags::FUA` on input based on
    /// the result of [`Server::can_fua`].
    fn trim(&self, count: u32, offset: u64, flags: Flags) -> Result<()> {
        unimplemented!()
    }

    /// This may be called once just before the plugin is unloaded from memory.
    fn unload() where Self: Sized { unimplemented!() }

    /// An optional version string which is displayed in help and debugging
    /// output.
    fn version() -> Option<&'static CStr> where Self: Sized { None }

    /// Write data to the backing store.
    ///
    /// The `flags` argument may include [`Flags::FUA`] based on the result of
    /// [`Server::can_fua`].
    ///
    /// The callback must write the entire range if it can.  If it, it should
    /// return an error.
    fn write_at(&self, buf: &[u8], offset: u64, flags: Flags) -> Result<()> {
        unimplemented!()
    }

    /// Write consecutive zeros to the backing store.
    ///
    /// The callback must write the whole region if it can. The NBD protocol
    /// doesn't allow partial writes (instead, these are errors).
    ///
    /// If this callback is omitted, or if it fails with `ENOTSUP` or
    /// `EOPNOTSUPP` , then [`Server::write_at`] will be used as an
    /// automatic fallback except when the client requested a fast zero.
    ///
    /// # Arguments
    ///
    /// * `count`:  Length of the region to write in bytes
    /// * `offset`: Offset of the region to write in the backing store.
    /// * `flags`:  May include [`Flags::MAY_TRIM`], [`Flags::FAST_ZERO`],
    ///   and/or [`Flags::FUA`].
    fn zero(&self, count: u32, offset: u64, flags: Flags) -> Result<()> {
        unimplemented!()
    }
}

macro_rules! opt_method {
    ( $self:ident, $method:ident ) => {
        if $self.$method {Some(ffi::$method)} else {None}
    };
    ( $self:ident, $method:ident, $ffi_method:ident ) => {
        if $self.$method {Some(ffi::$ffi_method)} else {None}
    }
}

/// Used by (`plugin!`)[macro.plugin.html], but should never be accessed
/// directly by the user.
#[doc(hidden)]
#[derive(Default)]
pub struct Builder {
    pub after_fork: bool,
    pub block_size: bool,
    pub cache: bool,
    pub can_cache: bool,
    pub can_extents: bool,
    pub can_flush: bool,
    pub can_fast_zero: bool,
    pub can_fua: bool,
    pub can_multi_conn: bool,
    pub can_trim: bool,
    pub can_write: bool,
    pub can_zero: bool,
    pub config: bool,
    pub config_complete: bool,
    pub config_help: bool,
    pub dump_plugin: bool,
    pub extents: bool,
    pub flush: bool,
    pub get_ready: bool,
    pub is_rotational: bool,
    pub load: bool,
    pub preconnect: bool,
    pub thread_model: bool,
    pub trim: bool,
    pub unload: bool,
    pub write_at: bool,
    pub zero: bool,
}

impl Builder {
    #[doc(hidden)]
    pub fn into_ptr<S: Server>(self) -> *const Plugin {
        INIT.call_once(|| {
            unsafe {
                assert!(!INITIALIZED);
                INITIALIZED = true;
                CONFIG = S::config;
                CONFIG_COMPLETE = S::config_complete;
                DUMP_PLUGIN = S::dump_plugin;
                GET_READY = S::get_ready;
                AFTER_FORK = S::after_fork;
                LOAD = S::load;
                OPEN = S::open;
                PRECONNECT = S::preconnect;
                THREAD_MODEL = S::thread_model;
                UNLOAD = S::unload;
            };
        });

        let name = S::name().as_ptr();
        let config_help = S::config_help()
            .map(CStr::as_ptr)
            .unwrap_or(ptr::null());
        let description = S::description()
            .map(CStr::as_ptr)
            .unwrap_or(ptr::null());
        let longname = S::longname()
            .map(CStr::as_ptr)
            .unwrap_or(ptr::null());
        let magic_config_key = S::magic_config_key()
            .map(CStr::as_ptr)
            .unwrap_or(ptr::null());
        let version = S::version()
            .map(CStr::as_ptr)
            .unwrap_or(ptr::null());
        let plugin = Plugin {
            _struct_size: mem::size_of::<Plugin>() as u64,
            _api_version: 2,
            _thread_model: ThreadModel::Parallel as c_int,
            name,
            longname,
            version,
            description,
            load: opt_method!(self, load),
            unload: opt_method!(self, unload),
            config: opt_method!(self, config),
            config_complete: opt_method!(self, config_complete),
            config_help,
            open: ffi::open,
            close: ffi::close,
            get_size: ffi::get_size,
            can_write: opt_method!(self, can_write),
            can_flush: opt_method!(self, can_flush),
            is_rotational: opt_method!(self, is_rotational),
            can_trim: opt_method!(self, can_trim),
            _pread_v1: None,
            _pwrite_v1: None,
            _flush_v1: None,
            _trim_v1: None,
            _zero_v1: None,
            errno_is_preserved: 0,
            dump_plugin: opt_method!(self, dump_plugin),
            can_zero: opt_method!(self, can_zero),
            can_fua: opt_method!(self, can_fua),
            pread: ffi::pread,
            pwrite: opt_method!(self, write_at, pwrite),
            flush: opt_method!(self, flush),
            trim: opt_method!(self, trim),
            zero: opt_method!(self, zero),
            magic_config_key,
            can_multi_conn: opt_method!(self, can_multi_conn),
            can_extents: opt_method!(self, can_extents),
            extents: opt_method!(self, extents),
            can_cache: opt_method!(self, can_cache),
            cache: opt_method!(self, cache),
            thread_model: opt_method!(self, thread_model),
            can_fast_zero: opt_method!(self, can_fast_zero),
            preconnect: opt_method!(self, preconnect),
            get_ready: opt_method!(self, get_ready),
            after_fork: opt_method!(self, after_fork),
            _list_exports: None, // not implemented
            _default_export: ptr::null(), // not implemented
            _export_description: ptr::null(), // not implemented
            _cleanup: None, // not implemented
            block_size: opt_method!(self, block_size)
        };
        // Leak the memory to C.  NBDKit will never give it back.
        Box::into_raw(Box::new(plugin))
    }

    #[doc(hidden)]
    pub fn new() -> Builder
    {
        Builder::default()
    }

}

// C functions provided by the nbdkit binary
extern "C" {
    fn nbdkit_add_extent(extents: *mut c_void, offset: u64, length: u64,
                         ty: u32) -> c_int;
    #[doc(hidden)]
    pub fn nbdkit_debug(fmt: *const c_char, ...);
    fn nbdkit_debug_hexdiff(buf1: *const c_void, buf2: *const c_void,
                            len: usize,
                            prefix: *const c_char, start: u64);
    fn nbdkit_debug_hexdump(buf: *const c_void, len: usize,
                            prefix: *const c_char, start: u64);
    fn nbdkit_error(fmt: *const c_char, ...);
    fn nbdkit_export_name() -> *const c_char;
    fn nbdkit_set_error(errno: c_int);
    #[cfg(feature = "nix")]
    fn nbdkit_peer_name( addr: *mut libc::sockaddr,
                         addrlen: *mut libc::socklen_t) -> c_int;
    fn nbdkit_shutdown();
    fn nbdkit_disconnect(force: bool);
    fn nbdkit_stdio_safe() -> c_int;
    fn nbdkit_is_tls() -> c_int;
    fn nbdkit_parse_bool(s: *const c_char) -> c_int;
    fn nbdkit_parse_size(s: *const c_char) -> i64;
    fn nbdkit_parse_probability(what: *const c_char,
                                s: *const c_char,
                                retp: *mut c_double) -> c_int;
    fn nbdkit_name() -> *const c_char;
    fn nbdkit_timestamp() -> *const c_char;
}

/// Print a debug message.
///
/// Debug messages are only output when the server runs in verbose mode
/// (-v flag).  They are sent to stderr if nbdkit is running in the
/// foreground, or to syslog if nbdkit is forked into the background.
#[macro_export]
macro_rules! debug {
    ($($args:tt)*) => {
        match ::std::ffi::CString::new(format!($($args)*)) {
            Ok(msg) =>
                unsafe {
                    $crate::nbdkit_debug(c"%s".as_ptr(), msg.as_ptr());
                },
            Err(_) =>
                // probably the string contained \0, ignore as it
                // is only a debug message
                ()
        }
    }
}

/// Display differences between two buffers of equal length
pub fn debug_hexdiff(buf1: &[u8], buf2: &[u8],
                     prefix: Option<&str>, start: u64) -> Result<()> {
    if buf1.len() != buf2.len() {
        return Err(Error::new(libc::EINVAL,
                              "buffers must have the same length"));
    }
    let prefix = match prefix {
        Some(p) => p.as_ptr(),
        None => ptr::null(),
    };
    unsafe {
        nbdkit_debug_hexdiff(
            buf1.as_ptr() as *const c_void,
            buf2.as_ptr() as *const c_void,
            buf1.len(),
            prefix as *const c_char,
            start,
        );
    };
    Ok(())
}

/// Display buffer in hexdump format
pub fn debug_hexdump(buf: &[u8], prefix: Option<&str>, start: u64) {
    let prefix = match prefix {
        Some(p) => p.as_ptr(),
        None => ptr::null(),
    };
    unsafe {
        nbdkit_debug_hexdump(
            buf.as_ptr() as *const c_void,
            buf.len(),
            prefix as *const c_char,
            start,
        );
    }
}

/// Return the optional NBD export name if one was negotiated with the current
/// client
///
/// Note that this function must be called from one of nbdkit's own threads.
/// That is, it can only be called in the same thread as one of the `Server`
/// callbacks.
pub fn export_name() -> std::result::Result<String, Box<dyn error::Error>> {
    unsafe {
        let p = nbdkit_export_name();
        if p.is_null() {
            return Err("No export name available".into());
        }
        CStr::from_ptr(p)
    }.to_str()
    .map(|s| s.to_owned())
    .map_err(|e| Box::new(e) as Box<dyn error::Error>)
}

/// Is it safe to interact with stdin and stdout during the configuration phase?
///
/// This function is only relevant up through `config_complete`.  After the
/// configuration phase, the client should assume that stdin and stdout have
/// been closed.
pub fn is_stdio_safe() -> bool {
    unsafe { nbdkit_stdio_safe() != 0 } // note: cannot return an error
}

/// Did the client complete TLS authentication?
///
/// Call this from the `open` method.  It returns true if the client
/// completed TLS authentication, or false if the connection is using
/// plaintext.
pub fn is_tls() -> Result<bool> {
    let r = unsafe { nbdkit_is_tls() };
    match r {
        0 => Ok(false),
        x if x > 0 => Ok(true),
        _ =>
            // nbdkit_error was called, so the real error was already
            // written on stderr or sent to the logs.
            Err(Error::new(libc::EINVAL, "nbdkit_is_tls failed"))
    }
}

/// Return the peer (client) address, if available.
///
/// Note that this function must be called from one of nbdkit's own threads.
/// That is, it can only be called in the same thread as one of the `Server`
/// callbacks.
#[cfg(feature = "nix")]
pub fn peername<T: SockaddrLike>() -> std::result::Result<T, Box<dyn error::Error>> {
    let mut s = mem::MaybeUninit::<T>::uninit();
    let mut len = T::size();
    unsafe {
        let sa = s.as_mut_ptr() as *mut libc::sockaddr;
        let r = nbdkit_peer_name(sa, &mut len as *mut _);
        if r == -1 {
            // Note that nbdkit_peer_name does _not_ set errno
            return Err("No peer name available".into());
        }
        T::from_raw(s.assume_init().as_ptr(), Some(len)).ok_or_else(|| {
            Box::new(Error::new(
                libc::EINVAL,
                "Invalid sockaddr returned by nbdkit_peer_name"
            )) as Box<dyn error::Error>
        })
    }
}

/// Request nbdkit to asynchronously and safely shutdown the server.
pub fn shutdown() {
    unsafe { nbdkit_shutdown() };
}

/// Request nbdkit to disconnect the current client.
pub fn disconnect(force: bool) {
    unsafe { nbdkit_disconnect(force) };
}

/// Parse a string as a boolean using `nbdkit_parse_bool`.
///
/// This parses boolean strings like `true`, `no`, etc in the same way as
/// other (non Rust) nbdkit plugins, so you can use this function for
/// consistency with other plugins.
pub fn parse_bool(s: &str) -> Result<bool> {
    let cs = CString::new(s).unwrap();
    let r = unsafe { nbdkit_parse_bool(cs.as_ptr()) };
    match r {
        -1 =>
            // nbdkit_error was called already.
            Err(Error::new(libc::EINVAL,
                           "failed to parse boolean using nbdkit_parse_bool")),
        0 => Ok(false),
        _ => Ok(true)
    }
}

/// Parse a string as a size using `nbdkit_parse_size`.
///
/// This parses size strings like `1M`, `7E`, etc in the same way as
/// other (non Rust) nbdkit plugins, so you can use this function for
/// consistency with other plugins.
pub fn parse_size(s: &str) -> Result<i64> {
    let cs = CString::new(s).unwrap();
    let r = unsafe { nbdkit_parse_size(cs.as_ptr()) };
    if r == -1 {
        // nbdkit_error was called already.
        return Err(
            Error::new(libc::EINVAL,
                       "failed to parse size using nbdkit_parse_size"));
    }
    Ok(r)
}

/// Parse a string as a probability/percentage using
/// `nbdkit_parse_probability`.
///
/// This parses probabilities like `0.1`, `1e-1`, `10%`, `1:10`, `1/10`.
pub fn parse_probability(what: &str, s: &str) -> Result<f64> {
    let ws = CString::new(what).unwrap();
    let cs = CString::new(s).unwrap();
    let mut d = 0.0;
    let r = unsafe { nbdkit_parse_probability(ws.as_ptr(), cs.as_ptr(),
                                              &mut d) };
    if r == -1 {
        // nbdkit_error was called already.
        return Err(
            Error::new(libc::EINVAL,
                       "failed to parse probability using nbdkit_parse_probability"));
    }
    Ok(d)
}

/// Return the name of the nbdkit instance.
pub fn name() -> Option<String> {
    // nbdkit_name() returning NULL is not an error.
    let p = unsafe { nbdkit_name() };
    if p.is_null() {
        None
    }
    else {
        // XXX We're ignoring unlikely UTF-8 conversion errors here.
        let s = unsafe { CStr::from_ptr(p) }.to_str().unwrap();
        Some(String::from(s))
    }
}

/// Generate a timestamp for log messages.
///
/// Note that this function must be called from one of nbdkit's own threads.
/// That is, it can only be called in the same thread as one of the `Server`
/// callbacks.
pub fn timestamp() -> String {
    // nbdkit_timestamp() cannot return NULL.
    // We're ignoring UTF-8 conversion errors, but they cannot happen.
    let s = unsafe {
        let p = nbdkit_timestamp();
        CStr::from_ptr(p)
    }.to_str().unwrap();
    String::from(s)
}

#[doc(hidden)]
#[repr(C)]
pub struct Plugin {
    // Do not modify these three fields directly.
    #[doc(hidden)] pub _struct_size: u64,
    #[doc(hidden)] pub _api_version: c_int,
    #[doc(hidden)] pub _thread_model: c_int,

    pub name: *const c_char,
    pub longname: *const c_char,
    pub version: *const c_char,
    pub description: *const c_char,

    pub load: Option<extern "C" fn ()>,
    pub unload: Option<extern "C" fn ()>,

    pub config: Option<extern "C" fn (*const c_char, *const c_char) -> c_int>,
    pub config_complete: Option<extern "C" fn () -> c_int>,
    pub config_help: *const c_char,

    pub open: extern "C" fn (c_int) -> *mut c_void,
    pub close: extern "C" fn (*mut c_void),

    pub get_size: extern "C" fn (*mut c_void) -> i64,

    pub can_write: Option<extern "C" fn (*mut c_void) -> c_int>,
    pub can_flush: Option<extern "C" fn (*mut c_void) -> c_int>,
    pub is_rotational: Option<extern "C" fn (*mut c_void) -> c_int>,
    pub can_trim: Option<extern "C" fn (*mut c_void) -> c_int>,

    // Slots for old v1 API functions.
    #[doc(hidden)] pub _pread_v1: Option<extern "C" fn ()>,
    #[doc(hidden)] pub _pwrite_v1: Option<extern "C" fn ()>,
    #[doc(hidden)] pub _flush_v1: Option<extern "C" fn ()>,
    #[doc(hidden)] pub _trim_v1: Option<extern "C" fn ()>,
    #[doc(hidden)] pub _zero_v1: Option<extern "C" fn ()>,

    #[doc(hidden)] pub errno_is_preserved: c_int,

    pub dump_plugin: Option<extern "C" fn ()>,

    pub can_zero: Option<extern "C" fn (*mut c_void) -> c_int>,
    pub can_fua: Option<extern "C" fn (*mut c_void) -> c_int>,

    pub pread: extern "C" fn (h: *mut c_void, buf: *mut c_char, count: u32,
                              offset: u64,
                              flags: u32) -> c_int,
    pub pwrite: Option<extern "C" fn (h: *mut c_void, buf: *const c_char,
                                      count: u32, offset: u64,
                                      flags: u32) -> c_int>,
    pub flush: Option<extern "C" fn (h: *mut c_void, flags: u32) -> c_int>,
    pub trim: Option<extern "C" fn (h: *mut c_void,
                                    count: u32, offset: u64,
                                    flags: u32) -> c_int>,
    pub zero: Option<extern "C" fn (h: *mut c_void,
                                    count: u32, offset: u64,
                                    flags: u32) -> c_int>,

    pub magic_config_key: *const c_char,

    pub can_multi_conn: Option<extern "C" fn (h: *mut c_void) -> c_int>,

    pub can_extents: Option<extern "C" fn (h: *mut c_void) -> c_int>,
    pub extents: Option<extern "C" fn (h: *mut c_void,
                                       count: u32,
                                       offset: u64,
                                       rawflags: u32,
                                       extent_handle: *mut c_void) -> c_int>,

    pub can_cache: Option<extern "C" fn (h: *mut c_void) -> c_int>,
    pub cache: Option<extern "C" fn (h: *mut c_void,
                                     count: u32, offset: u64,
                                     flags: u32) -> c_int>,

    pub thread_model: Option<extern "C" fn () -> c_int>,

    pub can_fast_zero: Option<extern "C" fn (h: *mut c_void) -> c_int>,
    pub preconnect: Option<extern "C" fn(readonly: c_int) -> c_int>,
    pub get_ready: Option<extern "C" fn() -> c_int>,
    pub after_fork: Option<extern "C" fn() -> c_int>,

    // These fields exist in the struct but have not been implemented
    // by the Rust bindings yet.
    #[doc(hidden)] pub _list_exports: Option<extern "C" fn ()>,
    #[doc(hidden)] pub _default_export: *const c_char,
    #[doc(hidden)] pub _export_description: *const c_char,
    #[doc(hidden)] pub _cleanup: Option<extern "C" fn ()>,

    pub block_size: Option<extern "C" fn(h: *mut c_void,
                                         minp: *mut u32,
                                         prefp: *mut u32,
                                         maxp: *mut u32) -> c_int>,
}

/// Register your plugin with NBDKit.
///
/// Declare which optional methods it supports by supplying each as an argument.
///
/// # Examples
///
/// ```
/// # use std::ffi::CStr;
/// # use nbdkit::*;
/// struct MyPlugin{
///     // ...
/// }
/// impl Server for MyPlugin {
///     fn get_size(&self) -> Result<i64> {
///         # unimplemented!();
///         // ...
///     }
///
///     fn name() -> &'static CStr {
///         c"my_plugin"
///     }
///
///     fn open(_readonly: bool) -> Result<Box<dyn Server>> {
///         # unimplemented!();
///         // ...
///     }
///
///     fn read_at(&self, buf: &mut [u8], offs: u64) -> Result<()> {
///         # unimplemented!();
///         // ...
///     }
///     fn write_at(&self, buf: &[u8], offs: u64, flags: Flags) -> Result<()> {
///         # unimplemented!();
///         // ...
///     }
/// }
///
/// plugin!(MyPlugin {write_at});
/// ```
#[macro_export]
macro_rules! plugin {
    ( $cls:path { $($feat:ident),* } ) => {
        #[no_mangle]
        pub extern "C" fn plugin_init () -> *const ::nbdkit::Plugin {
            let mut plugin = ::nbdkit::Builder::new();
            $(plugin.$feat = true;)*
            plugin.into_ptr::<$cls>()
        }
    }
}

#[cfg(test)]
mod t {
    #[cfg(feature = "nix")]
    mod peername {
        use super::super::*;
        use lazy_static::lazy_static;
        use memoffset::offset_of;
        use mockall::{mock, predicate::*};
        use nix::sys::socket::{SockaddrIn, SockaddrIn6, UnixAddr};
        use std::sync::Mutex;

        lazy_static! {
            /// Mediates access to MockNbdkit's global expectations
            /// Any test that sets an expectation on a static method
            /// grab this mutex
            static ref MOCK_NBDKIT_MTX: Mutex<()> = Mutex::new(());
        }

        mock! {
            pub Nbdkit {
                fn peer_name(addr: *mut libc::sockaddr,
                                    addrlen: *mut libc::socklen_t) -> c_int;
            }
        }

        #[no_mangle]
        extern "C" fn nbdkit_peer_name( addr: *mut libc::sockaddr,
                                        addrlen: *mut libc::socklen_t) -> c_int
        {
            MockNbdkit::peer_name(addr, addrlen)
        }

        #[test]
        fn error() {
            let _m = MOCK_NBDKIT_MTX.lock().unwrap();
            let ctx = MockNbdkit::peer_name_context();
            ctx.expect()
                // Since nbdkit_peer_name does not set errno, all types of
                // errors are indistinguishable to a plugin
                .return_const(-1);
            let e = peername::<SockaddrIn>().unwrap_err();
            ctx.checkpoint();
            assert_eq!("No peer name available", e.to_string());
        }

        #[test]
        fn in4() {
            let _m = MOCK_NBDKIT_MTX.lock().unwrap();
            let ctx = MockNbdkit::peer_name_context();
            ctx.expect()
                .withf(|_, len| {
                    let l = unsafe {**len as usize};
                    l == mem::size_of::<libc::sockaddr_in>()
                }).returning(|sa, sl| {
                    let sin = sa as *mut libc::sockaddr_in;
                    unsafe {
                        *sin = libc::sockaddr_in {
                            sin_family: libc::AF_INET as libc::sa_family_t,
                            sin_port: u16::from_ne_bytes([4, 0xd2]),
                            sin_addr: libc::in_addr {
                                s_addr: u32::from_ne_bytes([127, 0, 0, 1])
                            },
                            .. mem::zeroed()
                        };
                        *sl = mem::size_of::<libc::sockaddr_in>()
                            as libc::socklen_t;
                    }
                    0
                });
            assert_eq!("127.0.0.1:1234", peername::<SockaddrIn>().unwrap().to_string());
            ctx.checkpoint();
        }

        #[test]
        fn in6() {
            let _m = MOCK_NBDKIT_MTX.lock().unwrap();
            let ctx = MockNbdkit::peer_name_context();
            ctx.expect()
                .withf(|_, len| {
                       let l = unsafe {**len as usize};
                       l == mem::size_of::<libc::sockaddr_in6>()
                }).returning(|sa, sl| {
                    let sin6 = sa as *mut libc::sockaddr_in6;
                    unsafe {
                        *sin6 = libc::sockaddr_in6 {
                            sin6_family: libc::AF_INET6 as libc::sa_family_t,
                            sin6_port: u16::from_ne_bytes([4, 0xd2]),
                            sin6_addr: libc::in6_addr {
                                s6_addr: [0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 1],
                            },
                            .. mem::zeroed()
                        };
                        *sl = mem::size_of::<libc::sockaddr_in6>()
                            as libc::socklen_t;
                    }
                    0
                });
            assert_eq!("[::1]:1234", peername::<SockaddrIn6>().unwrap().to_string());
            ctx.checkpoint();
        }

        #[test]
        fn un() {
            let _m = MOCK_NBDKIT_MTX.lock().unwrap();
            let ctx = MockNbdkit::peer_name_context();
            ctx.expect()
                .withf(|_, len| {
                       let l = unsafe {**len as usize};
                       l == mem::size_of::<libc::sockaddr_un>()
                }).returning(|sa, sl| {
                    let sun = sa as *mut libc::sockaddr_un;

                    unsafe {
                        *sun = mem::zeroed();
                        (*sun).sun_family = libc::AF_UNIX as libc::sa_family_t;
                        ptr::copy_nonoverlapping(
                            c"rust-test.sock".as_ptr(),
                            (*sun).sun_path.as_mut_ptr(), 14);
                        let len = offset_of!(libc::sockaddr_un, sun_path) as u8
                            + 14;
                        #[cfg(any(target_os = "freebsd",
                                  target_os = "netbsd",
                                  target_os = "openbsd",
                                  target_os = "macos",
                                  target_os = "ios",
                                  target_os = "dragonfly"
                        ))]
                        {
                            (*sun).sun_len = len;
                        }
                        *sl = len as libc::socklen_t;
                    }
                    0
                });
            assert_eq!("rust-test.sock",
                       peername::<UnixAddr>().unwrap().to_string());
            ctx.checkpoint();
        }
    }
}
