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

requires_run
requires test -f disk
requires_nbdinfo
requires "$QEMU_IMG" --version
requires qemu-nbd --version
requires diff --version

# nbdinfo in RHEL 8 did not support [ .. ] to run an external server
# under systemd socket activation.
requires nbdinfo --map -- [ nbdkit --exit-with-parent null ]

qcow2=qcow2dec-map-disk.qcow2
map_expected=qcow2dec-map-expected.txt
map_actual=qcow2dec-map-actual.txt
files="$qcow2 $map_expected $map_actual"
rm -f $files
cleanup_fn rm -f $files

# Create a qcow2 disk for testing.
"$QEMU_IMG" convert -f raw disk -O qcow2 $qcow2 -c

# Get the expected map from qemu-nbd.
nbdinfo --map -- [ qemu-nbd -r -f qcow2 $qcow2 ] > $map_expected
cat $map_expected

# Get the actual map from nbdkit qcow2dec filter.
nbdkit file --filter=qcow2dec $qcow2 \
       --run 'nbdinfo --map "$uri"' > $map_actual
cat $map_actual

# Compare the two maps, they should be identical.
diff -u $map_expected $map_actual
