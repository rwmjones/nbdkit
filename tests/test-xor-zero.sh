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

# Test XOR of zero data.  This checks that we handle endianness
# correctly.

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

img=xor-zero.img
files="$img"
rm -f $files
cleanup_fn rm -f $files

export img
nbdsh -c - <<'EOF'
import os

img = os.getenv("img")

xor = [ 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef ]
xorstr = "0x0123456789abcdef"

def round_up(i, n):
    while i % n != 0: i = i+1
    return i

for xorlen in range(1, 8): # 1..7
    f = open(img, "a")
    f.truncate(1024*1024)
    f.close()

    h = nbd.NBD()
    h.connect_systemd_socket_activation([
        "nbdkit", "--exit-with-parent", "file", img,
        "--filter=xor", "xor=" + xorstr, "xorlen=" + str(xorlen)
    ])
    h.pwrite(bytearray(65536), 0)
    h.pwrite(bytearray(65536), 65537)  # not aligned
    h.zero(65536, 262144)
    h.zero(65536, 327681)  # not aligned
    h.trim(65536, 524288)
    h.trim(65536, 589825)  # not aligned
    h.close()

    # Check backing image contains XORed data.
    f = open(img, "rb")
    for i in [ 0, round_up(65537, xorlen),
               round_up(262144, xorlen), round_up(327681, xorlen),
               round_up(524288, xorlen), round_up(589825, xorlen) ]:
        f.seek(i)
        for j in range(0, 8): # 0..7
            for k in range(8-xorlen, 8):
                b = int.from_bytes(f.read(1), "little")
                print("(%d, %d, %d) = 0x%x (expecting 0x%x)" %
                      (i, j, k, b, xor[k]),
                      flush=True)
                assert(b == xor[k])
    f.close()

    os.remove(img)
EOF
