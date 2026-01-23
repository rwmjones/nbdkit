# Change Log

All notable changes to this project will be documented in this file.
This project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased] - ReleaseDate

### Added

- Added a binding for `nbdkit_debug_hexdiff`
  (#[138](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/138))

### Changed

- `Server` methods that return fixed strings, like `Server::name`, now return
  `&static CStr` strings instead of `&'static str`.
  (#[67](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/67))

- The MSRV is now 1.77.0
  (#[67](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/67))
  (#[49](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/49))
 
## [0.3.0] - 20 July 2023
### Added

- The `Server` trait now binds the nbdkit `block_size` method.
  (#[34](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/34))

- The C function `nbdkit_parse_probability` is now available as
  `nbdkit::parse_probability`.
  ([540b936f](https://gitlab.com/nbdkit/nbdkit/-/commit/540b936fc019b80ca53bd1ab66974f9b15fa4aae))

- The C functions `nbdkit_parse_bool` and `nbdkit_parse_size`
  are now available as `nbdkit::parse_bool` and `nbdkit::parse_size`.
  (#[29](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/29))

- The new `disconnect` method can disconnect the current client.
  ([6ac807a](https://gitlab.com/nbdkit/nbdkit/-/commit/6ac807afd89b76138776a00dc72296b9e308789b#e4c08f6fb1d46a0e2d31c745310e364555390ff0))

- The `Server` trait now has an `after_fork` method.
  ([d62f268](https://gitlab.com/nbdkit/nbdkit/-/commit/d62f26808ea1fa2cf97d990745b76010caffe7d4))

- The C function `nbdkit_is_tls` is now bound as `nbdkit::is_tls`.
  (#[25](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/25))

- The C function `nbdkit_debug` is now available (as `nbdkit::debug!`).
  ([#24](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/24))

### Changed

- The `open` method must now return a `Result<>`.  Existing plugins
  should be modified to use `Ok()` around the return value.
  ([#23](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/23))

- The `peername` function is now generic, taking any argument that implements
  `nix::sys::socket::SockaddrLike`.
  ([#4](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/4))

- Raised the MSRV to 1.46.0 due to bitflags bug #255.
  ([#1](https://gitlab.com/nbdkit/nbdkit/-/merge_requests/1))

