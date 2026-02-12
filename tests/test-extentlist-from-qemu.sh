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

# Test the extentlist filter with qemu-img map, commands suggested in
# the manual page.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_plugin ones
requires "$QEMU_IMG" --version
requires "$QEMU_IMG" map --help
requires jq --version
requires $CUT --version
requires tr --version
requires tail --version
requires diff --version

disk=./disk
requires test -r $disk

extents=test-extentlist-from-qemu.extents
extents_post=test-extentlist-from-qemu.map
files="$extents $extents_post"
rm -f $files
cleanup_fn rm $files

export CUT extents_post QEMU_IMG

# Get the size.
size=$( "$QEMU_IMG" info $disk --output=json | jq '.["virtual-size"]' )

# Process the original disk into an extent map.
$QEMU_IMG map $disk |
    tr -s " " | $CUT -d " " -f 1-2 | tail -n +2 > $extents

# Create a disk.
nbdkit -r ones $size --filter=extentlist extentlist=$extents \
       --run '
    $QEMU_IMG map "$uri" |
        tr -s " " | $CUT -d " " -f 1-2 | tail -n +2 > $extents_post
'

# Compare original and post processed extents.
diff -u $extents $extents_post
