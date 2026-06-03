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

# Test xor=rand, XORing with the output of a PRNG.

source ./functions.sh
set -e
set -x
set -u

# This won't work on Windows because systemd socket activation
# isn't supported.
if is_windows; then
   echo "$0: systemd socket activation not supported on Windows"
   exit 77
fi

requires_plugin file
requires_filter xor

requires nbdsh --version
# Requires explicit h.close() method, added in 1.16
requires_libnbd_version 1.16

files="xor-rand0.img xor-rand1.img xor-rand2.img xor-rand3.img"
rm -f $files
cleanup_fn rm -f $files

nbdsh -c - <<'EOF'
import os
import random
import sys

buf = b"12345678" * 16

for i in range(0,4):
    name = "xor-rand%d.img" % i

    f = open(name, "a")
    f.truncate(len(buf))
    f.close()

    h = nbd.NBD()
    h.connect_systemd_socket_activation([
        "nbdkit", "--exit-with-parent", "file", name,
        "--filter=xor", "xor=rand:1234"
    ])

    # Write each time a different way.  The resulting files
    # should be identical.  But this tests different kinds
    # of alignment.
    if i == 0:
        for j in range(0, len(buf)):
            h.pwrite(buf[j:j+1], j)
    elif i == 1:
        h.pwrite(buf[0:7], 0)
        h.pwrite(buf[7:9], 7)
        h.pwrite(buf[9:], 9)
    elif i == 2:
        for j in range(0, len(buf), 64):
            h.pwrite(buf[j:j+64], j)
    elif i == 3:
        j = 0
        count = len(buf)
        while count > 0:
            n = min(random.randint(1, 24), count)
            h.pwrite(buf[j:j+n], j)
            j += n
            count -= n
    else:
        assert False

    # Check we can read back what we wrote.  The filter doesn't
    # cache anything so we don't have to worry that we are
    # reading cached data here.
    buf2 = h.pread(len(buf), 0)
    print("i = %d: buf = %r" % (i, buf2), file=sys.stderr, flush=True)
    assert buf == buf2

    h.close()

# Check backing images have identical data.  Note we don't check
# what the data actually is here since that would involve running
# the xoshiro PRNG.

name = "xor-rand%d.img" % 0
f = open(name, "rb")
data_0 = f.read(len(buf))
print("i = %d: data = %r" % (0, data_0), file=sys.stderr, flush=True)

for i in range(1,4):
    name = "xor-rand%d.img" % i

    f = open(name, "rb")
    data_i = f.read(len(buf))
    print("i = %d: data = %r" % (i, data_i), file=sys.stderr, flush=True)
    assert data_0 == data_i
EOF
