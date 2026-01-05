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

# Demonstrate blocksize-policy rounding extents

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_plugin data
requires_nbdinfo
requires nbdsh --base-allocation --version
requires_nbdsh_uri

files="blocksize-policy-extents.out"
rm -f $files
cleanup_fn rm -f $files

if ! nbdinfo --help | grep -- --map ; then
    echo "$0: nbdinfo --map option required to run this test"
    exit 77
fi

# When the truncate filter rounds an unaligned file up, the client should
# still see aligned results.
nbdkit data "@32k 1" --filter=blocksize-policy --filter=truncate \
       round-up=512 blocksize-minimum=512 blocksize-error-policy=error \
       --run 'nbdinfo --map "$uri"' > blocksize-policy-extents.out
diff -u - blocksize-policy-extents.out <<EOF
         0       32768    3  hole,zero
     32768         512    0  data
EOF

# Also ensure that the server itself is not breaking alignment at 4G
# boundaries.
nbdkit data "@4G 1 @^512" --filter=blocksize-policy \
       blocksize-minimum=512 blocksize-error-policy=error \
       --run 'nbdinfo --map "$uri"' > blocksize-policy-extents.out
diff -u - blocksize-policy-extents.out <<EOF
         0  4294967296    3  hole,zero
4294967296         512    0  data
EOF

# Check that unaligned requests are rejected when required
define script <<\EOF
def print_extents(context, offset, extents, err):
  assert context == nbd.CONTEXT_BASE_ALLOCATION;
  print(extents)

h.set_strict_mode(0)
try:
  h.block_status(511, 512, print_extents)
except nbd.Error:
  print("detected misaligned count")
try:
  h.block_status(512, 511, print_extents)
except nbd.Error:
  print("detected misaligned offset")
h.block_status(513, 32256, print_extents)
h.block_status(1, 32768, print_extents)
EOF
export script
nbdkit data "@32k 1" --filter=blocksize-policy \
       blocksize-minimum=512 blocksize-error-policy=error \
       --run 'nbdsh --base-allocation -u "$uri" -c "$script"' \
       > blocksize-policy-extents.out
diff -u - blocksize-policy-extents.out <<EOF
detected misaligned count
detected misaligned offset
[512, 3]
[1, 0]
EOF

# Check that unaligned requests still work when permitted (a user could trigger
# an assertion failure prior to 1.48, as a minor security flaw)
define script <<\EOF
def print_extents(context, offset, extents, err):
  assert context == nbd.CONTEXT_BASE_ALLOCATION;
  print(extents)

h.set_strict_mode(0)
h.block_status(511, 512, print_extents)
h.block_status(512, 511, print_extents)
h.block_status(2, 32767, print_extents)
h.block_status(1, 32768, print_extents)
EOF
export script
nbdkit data "@32k 1" --filter=blocksize-policy \
       blocksize-minimum=512 blocksize-error-policy=allow \
       --run 'nbdsh --base-allocation -u "$uri" -c "$script"' \
       > blocksize-policy-extents.out
diff -u - blocksize-policy-extents.out <<EOF
[32256, 3]
[32257, 3]
[1, 3]
[1, 0]
EOF
