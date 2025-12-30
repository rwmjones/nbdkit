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

# Test nbdkit_debug_hexdump function.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdsh_uri
requires diff --version
requires $SED --version

plugin=.libs/test-debug-hexdump-plugin.$SOEXT
requires test -f $plugin

out=debug-hexdump.out
hexout=debug-hexdump.hexout
files="$out $hexout"
rm -f $files
cleanup_fn rm -f $files

define script <<'EOF'
import os

offset = int(os.getenv("offset"), 0)
count = int(os.getenv("count"), 0)

data = os.getenv("data")
b = bytearray(data, encoding='utf8')
if len(b) < count:
   b += bytearray(count - len(b))
assert(len(b) == count)

h.pwrite(b, offset)
EOF
export script

do_test ()
{
    count="$1"
    offset="$2"
    data="$3"
    export offset count data

    # Run nbdkit with the plugin and debug enabled.  Capture the full
    # output including stderr so we can find the hexdump output.
    fail=
    nbdkit -f -v $plugin --run 'nbdsh -u "$uri" -c "$script"' \
           >$out 2>&1 || fail=1
    cat $out
    if test "$fail"; then exit 1; fi

    # Get the hexdump lines from the output.
    grep "DUMP: " < $out | $SED 's/.*DUMP: //' > $hexout
    cat $hexout

    # Compare it to the expected output (in $expected variable).
    diff -u $hexout <(echo -n "$expected")
}

define expected <<'EOF'
00000000: 31                                                |1               |
EOF
do_test 1 0 '1'

define expected <<'EOF'
00000000:    31                                             | 1              |
EOF
do_test 1 1 '1'

define expected <<'EOF'
00000000:    31 32 33                                       | 123            |
EOF
do_test 3 1 '123'

define expected <<'EOF'
00000000:    31 32 33 00 00 00 00  00 00 00 00 00 00 00 00  | 123............|
00000010: 00                                                |.               |
EOF
do_test 16 1 '123'

define expected <<'EOF'
00000000: 31 32 33 61 62 63 01 00  00 00 00 00 00 00 00 00  |123abc..........|
EOF
do_test 16 0 $'123abc\x01'

define expected <<'EOF'
00000000: 31 32 33 00 00 00 00 00  00 00 00 00 00 00 00 00  |123.............|
00000010: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
EOF
do_test 32 0 '123'

define expected <<'EOF'
00000010: 31 32 33 00 00 00 00 00  00 00 00 00 00 00 00 00  |123.............|
EOF
do_test 16 16 '123'

define expected <<'EOF'
00000000:                          31 32 33 00 00 00 00 00  |        123.....|
00000010: 00 00 00 00 00 00 00 00                           |........        |
EOF
do_test 16 8 '123'

define expected <<'EOF'
00000000:                   31 32  33 00 00 00 00 00 00 00  |      123.......|
00000010: 00 00 00 00 00 00 00 00                           |........        |
EOF
do_test 18 6 '123'

define expected <<'EOF'
ffff00000000:                   31 32  33 00 00 00 00 00 00 00  |      123.......|
ffff00000010: 00 00 00 00 00 00 00 00                           |........        |
EOF
do_test 18 0xffff00000006 '123'

# XXX In future we might make hexdump choose the longest format for
# the offset field across all lines of output.
define expected <<'EOF'
fffffffffff0:                   31 32  33 00 00 00 00 00 00 00  |      123.......|
1000000000000: 00 00 00 00 00 00 00 00                           |........        |
EOF
do_test 18 0xfffffffffff6 '123'
