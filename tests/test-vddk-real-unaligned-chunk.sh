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

# Regression test for https://issues.redhat.com/browse/RHEL-71694

source ./functions.sh
set -e
set -x
set -u

requires_run
requires test "${vddkdir:-}" != ""
requires test -d "$vddkdir"
requires test -f "$vddkdir/lib64/libvixDiskLib.so"
requires "$QEMU_IMG" --version
requires_nbdinfo
requires $TRUNCATE --version
requires dd --version
requires test -r /dev/urandom
skip_if_valgrind "because setting LD_LIBRARY_PATH breaks valgrind"

# VDDK > 5.1.1 only supports x86_64.
if [ `uname -m` != "x86_64" ]; then
    echo "$0: unsupported architecture"
    exit 77
fi

d=vddk-real-unaligned-chunk.d
cleanup_fn rm -rf $d
rm -rf $d
mkdir $d

# Create a vmdk disk which is partially sparse and the size is NOT
# aligned to 128 sectors (chunk size).
dd if=/dev/urandom of=$d/test.raw bs=512 count=$(( 3*128 ))
$TRUNCATE -s $(( (4*128 + 3) * 512)) $d/test.raw
"$QEMU_IMG" convert -f raw $d/test.raw -O vmdk $d/test.vmdk

# Read the map using VDDK.
export d
nbdkit -rfv vddk libdir="$vddkdir" \
       $PWD/$d/test.vmdk \
       --run 'nbdinfo --map "$uri" > $d/map'
cat $d/map

# Note a few features of the expected map.  The first 3 chunks (3*128
# sectors) are allocated, followed by a single hole chunk.  Then the
# last 3 unaligned sectors appear allocated (even though they are not)
# because we could not read them using the QueryAllocatedBlocks API so
# we had to assume allocated.
test "$(cat $d/map)" = "\
         0      196608    0  data
    196608       65536    3  hole,zero
    262144        1536    0  data"
