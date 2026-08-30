# To-do list for nbdkit

## General ideas for improvements

* Listen on specific interfaces or protocols.

* Performance - measure and improve it.  Chart it over various buffer
  sizes and threads, as that should make it easier to identify
  systematic issues.

* For parallel plugins, only create threads on demand from parallel
  client requests, rather than pre-creating all threads at connection
  time, up to the thread pool size limit.  Of course, once created, a
  thread is reused as possible until the connection closes.

* A new threading model, `SERIALIZE_RETIREMENT`, which lets the client
  queue up multiple requests and processes them in parallel in the
  plugin, but where the responses sent back to the client are in the
  same order as the client's request rather than the plugin's
  completion.  This is stricter than fully parallel, but looser than
  `SERIALIZE_REQUESTS` (in particular, a client can get
  non-deterministic behavior if batched requests touch the same area
  of the export).

* Async callbacks.  The current parallel support requires one thread
  per pending message; a solution with fewer threads would split
  low-level code between request and response, where the callback has
  to inform nbdkit when the response is ready:
  https://www.redhat.com/archives/libguestfs/2018-January/msg00149.html

* More NBD protocol features.  The currently missing features are
  structured replies for sparse reads, and online resize.

* Test that zero-length read/write/extents requests behave sanely
  (NBD protocol says they are unspecified).

* If a client negotiates structured replies, and issues a read/extents
  call that exceeds EOF (qemu 3.1 is one such client, when nbdkit
  serves non-sector-aligned images), return the valid answer for the
  subset of the request in range and then `NBD_REPLY_TYPE_ERROR_OFFSET`
  for the tail, rather than erroring the entire request.

* Audit the code base to get rid of `strerror()` usage (the function is
  not thread-safe); however, using `strerror_r()` can be tricky as it
  has a different signature in glibc than in POSIX.

* Teach `nbdkit_error()` to have smart newline appending (for existing
  inconsistent clients), while fixing internal uses to omit the
  newline. Commit ef4f72ef has some ideas on smart newlines, but that
  should probably be factored into a utility function.

* Add a mode of operation where nbdkit is handed a pre-opened fd to be
  used immediately in transmission phase (skipping handshake).  There
  are already third-party clients of the kernel's `/dev/nbdX` which rely
  on their own protocol instead of NBD handshake, before calling
  `ioctl(NBD_SET_SOCK)`; this mode would let the third-party client
  continue to keep their non-standard handshake while utilizing nbdkit
  to prototype new behaviors in serving the kernel.

* `nbdkit.so`: nbdkit as a loadable shared library.  The aim of nbdkit
  is to make it reusable from other programs (see
  [nbdkit-captive(1)](https://libguestfs.org/nbdkit-captive.1.html)).
  If it was a loadable shared library it would be even more reusable.
  API would allow you to create an nbdkit instance, configure it (same
  as the current command line), start it serving on a socket, etc.
  However perhaps the current ability to work well as a subprocess is
  good enough?  Also allowing multiple instances of nbdkit to be
  loaded in the same process is probably impossible.

* Add separate man pages for a few missing `NBDKIT_EXTERN_DECL`
  functions, including those for extents, contexts, and exports.

* Enhance `--keepalive` by allowing more socket options to be set, in
  particular `SOL_TCP` + `TCP_KEEPCNT`, `SOL_TCP` + `TCP_KEEPIDLE`,
  and `SOL_TCP` + `TCP_KEEPINTVL`.

* Using `--port=0` (to get the kernel to pick a random port) works:

  - But the URI in `--print-uri` and `$uri` is wrong.  Fixing this is
    difficult because we generate and print the URI early, long before
    we bind to ports.  There is a case for binding to ports before
    closing stdio, but that is quite a large change and needs some
    careful thought.

  - Consider exposing `$port4` and `$port6` to `--run` scripts.  This
    would allow `tests/test-ip-filter.sh` to work.

* `nbdkit_timestamp()` does not work when called on the main thread.
  This is because the main thread does not allocate thread-local
  storage (see [threadlocal.c](server/threadlocal.c)).  We could
  either special-case this in `nbdkit_timestamp` or we could implement
  thread-local storage for the main thread (but the latter may change
  some assumptions in the code).  There may be other APIs that are
  affected in this way too.

## Suggestions for plugins

Note: qemu supports other formats such as iscsi and ceph/rbd, and
while similar plugins could be written for nbdkit there is no
compelling reason unless the result is better than qemu-nbd.  For the
majority of users it would be better if they were directed to qemu-nbd
for these use cases.

* XVA files

  https://lists.gnu.org/archive/html/qemu-devel/2017-11/msg02971.html
  is a partial solution but it needs cleaning up.

* OVA files

  OVAs are tar files, and could be automatically opened and all the
  disks inside exposed as separate exports.  This only really works
  well if the OVA contains raw format images (not VMDK which is normal
  for these files).  See also nbdkit-tar-filter.

* Imago plugin

  [Imago](https://docs.rs/imago/latest/imago/) is a Rust library that
  handles a bunch of common disk image formats like qcow2.  This could
  be exposed as a plugin.

nbdkit-floppy-plugin:

* Add boot sector support.  In theory this is easy (eg. using
  SYSLINUX), but the practical reality of making a fully bootable
  floppy is rather more complex.

* Add multiple dir merging.

* The test isn't very useful.  Try testing all the different file
  types we support, and some limits, like maximum file size.

nbdkit-linuxdisk-plugin:

* Add multiple dir merging (in e2fsprogs `mke2fs`).

VDDK:

* For testing VDDK, try to come up with delay filter + sparse plugin
  settings which behave closely (in terms of performance and API
  latency) to the real thing.  This would allow us to tune some
  performance tools without needing VMware all the time.

nbdkit-torrent-plugin:

* There are lots of settings we could map into parameters:
  https://www.libtorrent.org/reference-Settings.html#settings_pack

* The C++ could be a lot more natural.  At the moment it's a kind of
  “C with C++ extensions”.

nbdkit-ondemand-plugin:

* Implement more callbacks, eg. `.zero`

* Allow client to select size up to a limit, eg. by sending export
  names like `export:4G`.  This would have to be controlled by a
  flag, since existing clients might be using `':'`.

nbdkit-data-plugin:

* Allow expressions to evaluate to numbers, offsets, etc so that this
  works:

  `nbdkit data '(0x55 0xAA)*n' n=1000`

* Allow inclusion of files where the file is not binary but is written
  in the data format.

* Like `$VAR` but the variable is either binary or base64.

nbdkit-cdi-plugin:

* Look at using skopeo instead of podman pull
  (https://github.com/containers/skopeo)

nbdkit-blkio-plugin:

* Use event-driven mode instead of blocking mode.  This involves
  restructuring the plugin so that there is one or more background
  threads to handle the events, and nbdkit threads issue requests to
  these threads.  (See how it is done in the VDDK plugin.)

* [The current test](tests/test-blkio.sh) requires `io_uring`, but
  this is disabled in RHEL and not always available in Linux distros.
  Ideally blkio would support a simple "file" driver (using POSIX
  I/O), but it does not right now.  A simple project would be to
  contribute such a driver to libblkio, and then add a test to nbdkit.

* It would be good to add a way to detect what drivers are supported.
  This also requires changes to libblkio.

nbdkit-file-plugin:

* There are multiple issues with handling block devices, zeroing and
  trimming:

  - Extents don't work at all (eg. for LVM thin devices).  There's no
    way to fix this in the current Linux kernel.  Likely requires a
    patch such as: https://lkml.org/lkml/2024/3/28/1165

  - Alignment of discard and zero requests may be an issue, since the
    code currently assumes that sector alignment is always fine, but
    there may be block devices with larger alignment requirements.

  - We're waiting for Linux to allow `fallocate(FALLOC_FL_PUNCH_HOLE)`
    on all block device types (including LVM thin).  There are some
    proposals upstream.  This would allow us to remove the BLKDISCARD
    code and simplify things.

  - Can we be guaranteed that in all paths through `file_zero`, when
    trimming, reads will always return zeroes?  Requires code review.

  - For more, see comments in
    https://gitlab.com/nbdkit/nbdkit/-/merge_requests/88

nbdkit-vram-plugin:

* Investigate why, on AMD Radeon, trim does not immediately free Video
  RAM.  For some reason the Video RAM is only freed when you trim the
  entire device, not when you trim a part of the device.  (This might
  be an issue specific to the AMD driver, but that's the only driver I
  have to test.)

* radeontop says we're using 101% of the available Video RAM when we
  fully allocate a disk (without using the `size` parameter).
  Apparently this doesn't cause any issues, but why does it happen?

nbdkit-nfs-plugin:

* Implement extents and trim support.  See:
  https://github.com/sahlberg/libnfs/commit/af3d4279cba07c2766a93fb618f7508f7c031684

## Suggestions for language plugins

Python:

* Get the `__docstring__` from the module and print it in `--help`
  output.  This requires changes to the core API so that config_help
  is a function rather than a variable (see V3 suggestions below).

Golang:

* Remove the requirement for `CanWrite` to be overridden before
  `PWrite` is called (and the same for the other `Can*`).

  - In theory this could be done by [using reflection to tell if
    `PWrite` has been overridden in the
    subclass](https://stackoverflow.com/a/55205550).

  - I tried to implement this and the simple approach does not work.
    We do not know the derived `Connection` class until `Open` is
    called, which is far too late for registering callbacks.

  - But we might be able to use this trick to have the `Can*`
    functions in the base class simulate the internals of nbdkit,
    similar to how cc plugin has to do things (commit a31efe3338).

## Suggestions for filters

* Add shared filter.  Take advantage of filter context APIs to open a
  single context into the backend shared among multiple client
  connections.  This may even allow a filter to offer a more parallel
  threading model than the underlying plugin.

* CBT filter to track dirty blocks.  See these links for inspiration:
  https://www.cloudandheat.com/block-level-data-tracking-using-davice-mappers-dm-era/
  https://github.com/qemu/qemu/blob/master/docs/interop/bitmaps.rst

* masking plugin features for testing clients (see `nozero` and `fua`
  filters for examples)

* "bandwidth quota" filter which would close a connection after it
  exceeded a certain amount of bandwidth up or down.

* "forward-only" filter.  This would turn random access requests from
  the client into serial requests in the plugin, meaning that the
  plugin could be written to assume that requests only happen from
  beginning to end.  This is would be useful for plugins that have to
  deal with non-seekable compressed data.  Note the filter would have
  to work by caching already-read data in a temporary file.

* nbdkit-cache-filter should handle `ENOSPC` errors automatically by
  reclaiming blocks from the cache

* nbdkit-cache-filter could use a background thread for reclaiming.

* zstd filter was requested as a way to do what we currently do with
  xz but saving many hours on compression (at the cost of hundreds of
  MBs of extra data)
  https://github.com/facebook/zstd/issues/395#issuecomment-535875379

* nbdkit-exitlast-filter could probably use a configurable timeout so
  that there is a grace period in case another connection comes along.

* nbdkit-pause-filter would probably be more useful if the filter
  could inject a flush after pausing.  However this requires that
  filter background threads have access to the plugin (see above).

nbdkit-luks-filter:

* This filter should also support LUKSv2 (and so should qemu).

* There are some missing features: ESSIV, more ciphers.

* Implement `.trim` and `.zero` if possible.

nbdkit-readahead-filter:

* The filter should open a new connection to the plugin per background
  thread so it is able to work with plugins that use the
  serialize_requests thread model (like curl).  At the moment it makes
  requests on the same connection, so it requires plugins to use the
  parallel thread model.

* It should combine (or avoid) overlapping cache requests.

nbdkit-rate-filter:

* allow other kinds of traffic shaping such as VBR

* limit traffic per client (ie. per IP address)

* split large requests to avoid long, lumpy sleeps when request size
  is much larger than rate limit

nbdkit-retry-filter:

* allow user to specify which errors cause a retry and which ones are
  passed through; for example there's probably no point retrying on
  `ENOMEM`

* there are all kinds of extra complications possible here,
  eg. specifying a pattern of retrying and reopening:
  `retry-method=RRRORRRRRORRRRR` meaning to retry the data command 3
  times, reopen, retry 5 times, etc.

* subsecond times

nbdkit-ip-filter:

* permit hostnames and hostname wildcards to be used in the
  allow and deny lists

* the allow and deny lists should be updatable while nbdkit is
  running, for example by storing them in a database file

nbdkit-extentlist-filter:

* make non-read-only access safe by updating the extent list when the
  filter sees writes and trims

nbdkit-exportname-filter:

* find a way to call the plugin's `.list_exports` during `.open`, so that
  we can enforce `exportname-strict=true` without command line redundancy

* add a mode for passing in a file containing exportnames in the same
  manner accepted by the sh/eval plugins, rather than one name (and no
  description) per config parameter

nbdkit-evil-filter:

* improve the algorithm so it can simulate bursts of corrupted bits
  (https://listman.redhat.com/archives/libguestfs/2023-May/031567.html)

nbdkit-qcow2dec-filter:

* implement extents - we know which clusters are preallocated and
  sparse, so map that into extent information

* implement internal snapshots - it should be possible to select an
  internal snapshot by name

nbdkit-spinning-filter:

* fix multiconn / multiple connections so there is a single view of
  heads across all NBD clients

nbdkit-time-limit-filter:

* start counting at preconnect

* fix idle connection issue mentioned in the man page

* separate overall time limit and idle time limit

nbdkit-openonce-filter:

* Is TLS handling correct?  We might leak information between TLS and
  non-TLS contexts to clients.

nbdkit-map-filter:

* In theory it is possible to use this to convert disk formats without
  copying, eg. converting raw -> qcow2.  However to actually implement
  that we would need a way for this filter to inject new content (the
  qcow2 headers).  How would we do that?

Filters for security
--------------------

Things like filtering by IP address can be done using external
wrappers (TCP wrappers, systemd), or nbdkit-ip-filter.

However it might be nice to have a configurable filter for preventing
valid but not sensible requests.  The server already filters invalid
requests.  This would be like seccomp, and could be implemented using
an eBPF-based parser.  Unfortunately actual eBPF is difficult to use
for userspace processes.  The "standard" isn't solidly defined - the
Linux kernel implementation is the standard - and Linux has by far the
best implementation, particularly around bytecode verification and
JITting.  There is a userspace VM (ubpf) but it has very limited
capabilities compared to Linux.

Composing nbdkit
----------------

Filters allow certain types of composition, but others would not be
possible, for example RAIDing over multiple nbd sources.  Because the
plugin API limits us to loading a single plugin to the server, the
best way to do this (and the most robust) is to compose multiple
nbdkit processes.  Perhaps libnbd will prove useful for this purpose.

Build-related
-------------

* Figure out how to get `make distcheck` working. VPATH builds are
  working, but various pkg-config results that try to stick
  bash-completion and ocaml add-ons into their system-wide home do
  not play nicely with `--prefix` builds for a non-root user.

* Right now, `make check` builds keys with an expiration of 1 year
  only if they don't exist, and we leave the keys around except under
  `make distclean`.  This leads to testsuite failures when
  (re-)building in an incremental tree first started more than a year
  ago.  Better would be having make unconditionally run the generator
  scripts, but tweak the scripts themselves to be a no-op unless the
  keys don't exist or have expired.

Windows port
------------

Currently many features are missing, including:

* Daemonization.  This is not really applicable for Windows where you
  would instead want to run nbdkit as a service using something like
  SRVANY.  You must use the `-f` option or one of the other options
  that implies `-f`.

* These options are all unimplemented: `--exit-with-parent`,
  `--group`, `--run`, `--selinux-label`, `--single`, `--swap`,
  `--user`, `--vsock`

* Many other plugins and filters.

* `.errno_is_preserved` should use `GetLastError` and/or
  `WSAGetLastError` but currently does neither so errors from plugins
  are probably wrong in many cases.

* Most tests are skipped because of the missing features above.

V3 plugin protocol
------------------

From time to time we may update the plugin protocol.  This section
collects ideas for things which might be fixed in the next version of
the protocol.

Note that we keep the old protocol(s) around so that source
compatibility is retained.  Plugins must opt in to the new protocol
using `#define NBDKIT_API_VERSION <version>`

* All methods taking a `count` field should be `uint64_t` (instead of
  `uint32_t`).  Although the NBD protocol does not support 64 bit
  lengths, it might do in future.

* v2 `.can_zero` is tri-state for filters, but bool for plugins; in
  v3, even plugins should get a say on whether to emulate

* v2 `.can_extents` is bool, and cannot affect our response to
  `NBD_OPT_SET_META_CONTEXT` (that is, we are blindly emulating extent
  support regardless of the export name).  In v3, `.can_extents`
  should be tri-state, with an explicit way to disable that context on
  a per-export basis.

* `.pread` could be changed to allow it to support Structured Replies
  (SRs).  This could mean allowing it to return partial data, holes,
  zeroes, etc.  For a client that negotiates SR coupled with a plugin
  that supports .extents, the v2 protocol would allow us to at least
  synthesize `NBD_REPLY_TYPE_OFFSET_HOLE` for less network traffic,
  even though the plugin will still have to fully populate the .pread
  buffer; the v3 protocol should make sparse reads more direct.

* Parameters should be systematized so that they aren't just (key,
  value) strings.  nbdkit should know the possible keys for the plugin
  and filters, and the type of the values, and both check and parse
  them for the plugin.

* Modify `.open` so it takes an export name and tls parameter.

* Modify `.get_ready` to pass final selected thread model.  Filters
  already get this information, but to date, we have not yet seen a
  reason to add `nbdkit_get_thread_model` for use by v2 plugins.

* Change `config_help` from a variable to a function.

* Consider extra parameters to `.block_size`.  First so that we can
  separately control maximum data request (eg. pread) vs maximum
  virtual request (eg. zero).  Note that the NBD protocol does not yet
  support this distinction.  We may also need a `bool strict`
  parameter to specify whether the client requested block size
  information or not.  qemu-nbd can distinguish these two cases.

* Renumber thread models.  Redefine existing thread models like this:

  ```
   model                    existing value      new value
  SERIALIZE_CONNECTIONS           0               1000
  SERIALIZE_ALL_REQUESTS          1               2000
  SERIALIZE_REQUESTS              2               3000
  PARALLEL                        3               4000
  ```

  This allows new thread models to be inserted before and between the
  existing ones.  In particular `SERIALIZE_RETIREMENT` has been
  suggested above (inserted between `SERIALIZE_REQUESTS` and
  `PARALLEL`).  We could also imagine a thread model more like the one
  VDDK really wants which calls open and close from the main thread.

  For backwards compatibility the old numbers 0-3 can be transparently
  mapped to the new values.
