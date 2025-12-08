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

source ./functions.sh
set -e
set -x
set -u

# Requires the Unix version of the file plugin that supports the dir
# parameter.  XXX Maybe we will fix the winfile plugin in future.
if is_windows; then
   echo "$0: the winfile plugin does not support dir parameter"
   exit 77
fi

requires_nbdsh_uri
requires_plugin file
requires $TRUNCATE --version
requires $STAT --version

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
dir=cow-export-safe.d
rm -rf $sock $dir
cleanup_fn rm -rf $sock $dir
mkdir $dir

# Create two test disks that we will need separate overlays for.
$TRUNCATE -s $((1024*1024)) $dir/file1
$TRUNCATE -s $((2*1024*1024)) $dir/file2

# Run nbdkit with a COW overlay on top of the file plugin.
start_nbdkit -P $dir/pid -U $sock --filter=cow file dir=$dir

# Do the tests.
export sock
nbdsh -c - <<'EOF'
import os
sock = os.getenv("sock")

h.connect_uri("nbd+unix:///file1?socket=" + sock)
h2 = nbd.NBD()
h2.connect_uri("nbd+unix:///file2?socket=" + sock)

assert h.get_size() == 1024*1024
assert h2.get_size() == 2*1024*1024

# Check both files read back as zero.
assert h.pread(1024*1024, 0) == bytearray(1024*1024)
assert h2.pread(2*1024*1024, 0) == bytearray(2*1024*1024)

# Write to the first file, second file should not see the write.
buf = b"1" * (1024*1024)
h.pwrite(buf, 0)
assert h2.pread(1024*1024, 0) == bytearray(1024*1024)

# Write different data to the second file, first file should see
# what was written above.
buf2 = b"2" * (2*1024*1024)
h2.pwrite(buf2, 0)
assert h.pread(1024*1024, 0) == buf

# Second file should see the writes.
assert h2.pread(2*1024*1024, 0) == buf2

EOF

# The original files should both still be empty.
test "$($STAT -c %b $dir/file1)" = "0"
test "$($STAT -c %b $dir/file2)" = "0"
