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

# Test the readonly filter.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdinfo
requires_nbdsh_uri

# Check nbdinfo supports the --is flag (not RHEL 8).
requires nbdkit -r null --run 'nbdinfo --is readonly "$uri"'

# When used in unconditional mode, we advertise r/o to the client.
nbdkit null 1M --filter=readonly \
       --run 'nbdinfo --is readonly "$uri"'

# When not used in unconditional mode, we deny individual requests
# but do not advertise r/o if the underlying plugin is writable.
nbdkit null 1M --filter=readonly readonly-file=/nosuchfile \
       --run '! nbdinfo --is readonly "$uri"'

# If the underlying plugin is read-only then we advertise r/o either way.
nbdkit info --filter=readonly \
       --run 'nbdinfo --is readonly "$uri"'
nbdkit info --filter=readonly readonly-file=/nosuchfile \
       --run 'nbdinfo --is readonly "$uri"'

# Check we can flip between read-only and writable modes.
f=test-readonly.sentinel
cleanup_fn rm -f $f
rm -f $f

define script <<'EOF'

import os

f = os.getenv('f')

# Note this is always false even if the sentinel file exists.
assert not h.is_read_only()

# Writes should be permitted now.
h.pwrite(bytearray(512), 0)
h.flush()

# Create the sentinel file.
open(f, 'a').close()

# Writes and write-like operations should be denied.
try:
    h.pwrite(bytearray(512), 0)
    assert False
except nbd.Error as ex:
    assert ex.errno == "EPERM"
try:
    h.zero(512, 512)
    assert False
except nbd.Error as ex:
    assert ex.errno == "EPERM"
try:
    h.trim(512, 1024)
    assert False
except nbd.Error as ex:
    assert ex.errno == "EPERM"

# Reads should still be permitted.
buf = h.pread(512, 0)

# Remove the sentinel file.
os.remove(f)

# Writes should be permitted now.
h.pwrite(bytearray(512), 0)

EOF

export f script
nbdkit -v null 1M --filter=readonly readonly-file=$f \
       --run 'nbdsh -u "$uri" -c "$script"'
