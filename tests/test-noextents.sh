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
requires nbdsh --base-allocation --version
requires_nbdsh_uri

output=test-noextents.out
cleanup_fn rm -f $output

define script <<\EOF
def print_extents(context, offset, extents, err):
  assert context == nbd.CONTEXT_BASE_ALLOCATION;
  print(extents)

print(h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION))
if h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION):
  h.block_status(1024*1024, 0, print_extents)
  h.block_status(h.get_size() - 1024*1024, 1024*1024, print_extents)
EOF
export script

# Baseline: null supports extents that provide information beyond the request
nbdkit null 4M \
       --run 'nbdsh --base-allocation -u "$uri" -c "$script"' > $output
diff -u - $output <<\EOF
True
[4194304, 3]
[3145728, 3]
EOF

# With no parameters, extents are data only, and capped to request length
nbdkit null 4M --filter=noextents \
       --run 'nbdsh --base-allocation -u "$uri" -c "$script"' > $output
diff -u - $output <<\EOF
True
[1048576, 0]
[3145728, 0]
EOF
