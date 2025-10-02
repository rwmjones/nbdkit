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
requires_nbdcopy
requires_nbdinfo
requires "$QEMU_IMG" --version
requires cmp --version

# Check nbdinfo supports the --is flag (not RHEL 8).
requires nbdkit -r null --run 'nbdinfo --is readonly "$uri"'

qcow2=qcow2dec-disk.qcow2
raw=qcow2dec-disk.raw
pid=qcow2dec.pid
sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$qcow2 $raw $pid $sock"
rm -f $files
cleanup_fn rm -f $files

# Create a qcow2 disk for testing.
"$QEMU_IMG" convert -f raw disk -O qcow2 $qcow2 -c
#"$QEMU_IMG" convert -f raw disk -O qcow2 $qcow2 -c -o compression_type=zstd

# Start nbdkit.
start_nbdkit -P $pid -U $sock \
       --filter=qcow2dec \
       file $qcow2
uri="nbd+unix:///?socket=$sock"

# Check we can open it with nbdinfo, and that it is read-only.
nbdinfo "$uri"
nbdinfo --is readonly "$uri"

# Copy out all the data and compare to the original disk.
nbdcopy "$uri" $raw
cmp disk $raw
