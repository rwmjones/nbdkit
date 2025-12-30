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

# Test nbdkit_debug_hexdiff function.
#
# We assume that the tests of nbdkit_debug_hexdump do most of the
# heavy lifting as the two functions are very similar.  This only
# tests the different behaviour of hexdiff.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdsh_uri
requires diff --version
requires $SED --version

plugin=.libs/test-debug-hexdiff-plugin.$SOEXT
requires test -f $plugin

out=debug-hexdiff.out
hexout=debug-hexdiff.hexout
files="$out $hexout"
rm -f $files
cleanup_fn rm -f $files

define script <<'EOF'
import os

# Write zeros over zeroes
b = bytearray(32)
h.pwrite(b, 0)

# Write 4 lines over zeroes
b = b"1234567890abcdef" * 4
h.pwrite(b, 0)

# Partially overwrite second line
b = b"56780000"
h.pwrite(b, 20)

# Partially overwrite third and fourth lines
b = b"56780000cdef1234555590"
h.pwrite(b, 36)

EOF
export script

define expected <<'EOF'
# Write zeros over zeroes
 00000000: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
 00000010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|

# Write 4 lines over zeroes
-00000000: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
+00000000: 31 32 33 34 35 36 37 38  39 30 61 62 63 64 65 66  |1234567890abcdef|
-00000010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
+00000010: 31 32 33 34 35 36 37 38  39 30 61 62 63 64 65 66  |1234567890abcdef|
-00000020: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
+00000020: 31 32 33 34 35 36 37 38  39 30 61 62 63 64 65 66  |1234567890abcdef|
-00000030: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
+00000030: 31 32 33 34 35 36 37 38  39 30 61 62 63 64 65 66  |1234567890abcdef|

# Partially overwrite second line
-00000010:             35 36 37 38  39 30 61 62              |    567890ab    |
+00000010:                          30    30 30              |        0 00    |

# Partially overwrite third and fourth lines
-00000020:             35 36 37 38  39 30 61 62 63 64 65 66  |    567890abcdef|
+00000020:                          30    30 30              |        0 00    |
-00000030: 31 32 33 34 35 36 37 38  39 30                    |1234567890      |
+00000030:                35 35 35                           |     555        |
EOF

# Run nbdkit with the plugin and debug enabled.  Capture the full
# output including stderr so we can find the hexdump output.
fail=
nbdkit -f -v $plugin --run 'nbdsh -u "$uri" -c "$script"' >$out 2>&1 || fail=1
cat $out
if test "$fail"; then exit 1; fi

# Get the hexdiff lines from the output.
grep "DIFF: " < $out | $SED 's/.*DIFF: //' > $hexout
cat $hexout

# Compare it to the expected output (in $expected variable).
diff -u $hexout <(echo -n "$expected" | grep -v '^#' | grep -v '^$')
