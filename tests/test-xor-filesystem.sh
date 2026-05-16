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

# Test the xor filter for creating a complicated filesystem backed by
# XORed data, possibly of non-aligned length.

source ./functions.sh
set -e
set -x
set -u

requires test -d "$srcdir"
inp="$srcdir/../filters/xor/xor.c"
requires test -f "$inp"

requires head --version
requires hexdump --version
requires guestfish --version
requires md5sum --version
requires tail --version
requires $TRUNCATE --version

requires_plugin file
requires_filter xor

img=xor-filesystem.img
out1=xor-filesystem.out1
out2=xor-filesystem.out2
pid=xor-filesystem.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$img $out1 $out2 $pid $sock"
rm -f $files
cleanup_fn rm -f $files

$TRUNCATE -s 100M $img

# Choose a random XOR length of [1..8].
xorlen=$(( (RANDOM % 8) + 1 ))

start_nbdkit -P $pid -U $sock file $img \
             --filter=xor xor=0x123456789abcdef0 xorlen=$xorlen

guestfish -x <<EOF
add "" protocol:nbd server:unix:$sock format:raw discard:besteffort
run
part-disk /dev/sda gpt
mkfs ext4 /dev/sda1
mount-options "discard" /dev/sda1 /

# Fill the directory with empty files to test general
# filesystem consistency.
fill-dir / 10000

# Copy in a known file.  We copy it out and compare below.
copy-in "$inp" /

# Create an empty file of zeroes.
fill 0 10M /zero

# Wipe the local cache so we reread from the disk.
sync
drop-caches 3

ls / | tail
download /xor.c $out1
download /zero $out2
EOF

# Dump the first few lines of the image file for information.
hexdump -C $img | head -100

test "$(md5sum < $out1)" = "$(md5sum < "$inp")"

# Check the file of zeroes is really all zero.
test "$(hexdump -C $out2)" = "00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000"
