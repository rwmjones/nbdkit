#!/usr/bin/env bash
# nbdkit
# Copyright Red Hat
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

# Test exports / export name.

source ./functions.sh
set -e
set -x
set -u

skip_if_valgrind "because Golang code leaks memory"

requires_nbdsh_uri
# Requires explicit h.close() method, added in 1.16
requires_libnbd_version 1.16

plugin=../plugins/golang/testplugins/exports/nbdkit-goexports-plugin.so
requires test -r $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid=golang-exports.pid
rm -f $pid
cleanup_fn rm -f $pid $sock

define script <<'EOF'
import os
import sys

sock = os.getenv("sock")

errs = 0

for export in ("", "test1", "long" * 1023):
    print("Testing export: %s" % export, flush=True)

    uri = "nbd+unix:///" + export + "?socket=" + sock
    h = nbd.NBD()
    h.connect_uri(uri)

    # The disk size should be the same as the export name we sent.
    size = h.get_size()
    if size != len(export):
        print("Actual size: %r != Expected size %r" % (size, len(export)),
              flush=True)
        errs = errs+1

    # The content should be the same as the export name.
    if size != 0:
        buf = h.pread(size, 0)
        if buf != export.encode("utf-8"):
            print("Actual: %r != Expected %r" % (buf, export), flush=True)
            errs = errs+1

    h.close()

if errs != 0:
   print("FAIL: test failed, see earlier errors", flush=True)
   sys.exit(1)
EOF

# Golang requires us to run nbdkit in the foreground.
start_nbdkit_foreground -P $pid -U $sock $plugin

export sock
nbdsh -c "$script"
