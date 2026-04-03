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

# Test cow filter with huge overlays.  This used to break because of
# file size limits on /var/tmp.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdsh_uri

# ext4 has a 16T file size limit.
size=17T

define script <<'EOF'

import random

random.seed(None)

M = 1024*1024
T = 1024*1024*1024*1024

# Test a few megabytes either side of the 16T boundary.
overlay = bytearray(10 * M)
overlay_offset = int(16 * T - len(overlay)/2)
assert h.get_size() > overlay_offset + len(overlay)

for iter in range(1, 200):
    offset = random.randint(0, len(overlay) - 10)
    count = min(len(overlay) - offset, random.randint(1, 1 * M))
    buf = h.pread(count, overlay_offset + offset)
    assert buf == overlay[offset:offset+count]
    c = random.randint(1, 255)
    buf = bytearray([c]) * count
    h.pwrite(buf, overlay_offset + offset)
    overlay[offset:offset+count] = buf
    buf = h.pread(count, overlay_offset + offset)
    assert buf == overlay[offset:offset+count]

    if random.randint(0, 10) >= 7:
        offset = random.randint(0, len(overlay) - 10)
        count = min(len(overlay) - offset, random.randint(1, 1 * M))
        zbuf = bytearray(count)
        h.trim(count, overlay_offset + offset)
        overlay[offset:offset+count] = zbuf
        buf = h.pread(count, overlay_offset + offset)
        assert buf == zbuf

EOF
export script

nbdkit -fv \
       --filter=cow -D cow.verbose=1 \
       null $size \
       --run ' nbdsh -u "$uri" -c "$script" '
