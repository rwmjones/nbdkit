module libguestfs.org/nbdkit/go-nbd-test

// The minimum version specified by go-nbd is 1.21.12.  However gcc-go
// can't parse 3-element versions so we must do this:
go 1.22

require github.com/digitalocean/go-nbd v0.3.0
