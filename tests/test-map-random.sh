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

# Randomized tests of the map filter.

source ./functions.sh
set -e
set -x
set -u

requires_plugin sparse-random
requires_nbdsh_uri
# Uses explicit h.close() method, added in 1.16
requires_libnbd_version 1.16
requires_single_mode

# Use a random seed for the test so the test could be reproduced
# by setting this seed.
seed=$RANDOM
export seed

nbdsh -c - <<'EOF'
import os
import random

seed = int(os.getenv("seed"))
random.seed(seed)

# Tunables for sparse-random plugin.
max_size=2**32
runlength=65536
percent=50

size = random.randint(1024, max_size)
nr_maps = random.randint(0, 100)

maps = []
maps_cli = []
for i in range(0, nr_maps):
    start = random.randint(0, size-1024)
    end = random.randint(start, size-1)
    len = end-start+1
    dest = random.randint(0, size-len)
    maps += [(start, end, dest, len)]
    maps_cli += ["map=%d-%d:%d" % (start, end, dest)]

# We will run two copies of nbdkit.  One will only be the sparse-random
# plugin, thus exposing the underlying plugin data/extents directly.
# The other will have the map filter with the random map on top.
# Since the seed is the same for both, the underlying content is the same.

h1 = nbd.NBD()
h1.add_meta_context("base:allocation")
args = ["nbdkit", "-s", "--exit-with-parent", "-v",
        "sparse-random", str(size),
        "seed=" + str(seed),
        "runlength=" + str(runlength),
        "percent=" + str(percent)]
print("%r" % args, flush=True)
h1.connect_command(args)

h2 = nbd.NBD()
h2.add_meta_context("base:allocation")
args += ["--filter=map"]
args += maps_cli
print("%r" % args, flush=True)
h2.connect_command(args)

# Function to map location of a byte to its position in the underlying disk.
# Recall the rule that first matching map wins.
def map_offset(offset):
    for (start, end, dest, len) in maps:
        if start <= offset and offset <= end:
            mapped = dest + offset-start
            print("map %d -> %d because of rule map=%d-%d:%d" % \
                (offset, mapped, start, end, dest))
            return mapped
    # No explicit mapping, use implicit 1-1 mapping.
    return offset

# Read the data at an offset (inefficiently).
def get_byte(h, offset):
    b = h.pread(1, offset)
    return b[0]

# Read the sparseness at an offset (inefficiently).
def get_sparse(h, offset):
    is_sparse = None
    def f(mc, offs, e, err):
        nonlocal is_sparse
        is_sparse = e[1] & 3
    h.block_status(1, offset, f)
    assert is_sparse is not None
    return is_sparse != 0  # 0 = data, 3 = hole

# Read some random data from the filter and compare it to what we expect.
for t in range(0, 100):
    offset = random.randint(0, size-1)
    mapped_offset = map_offset(offset)

    expected = get_byte(h1, mapped_offset)
    expected_sparse = get_sparse(h1, mapped_offset)
    actual = get_byte(h2, offset)
    actual_sparse = get_sparse(h2, offset)

    print("test: offset %d expected: %r actual: %r" % \
        (offset, (expected, expected_sparse), (actual, actual_sparse)),
        flush=True)
    assert expected == actual
    assert expected_sparse == actual_sparse

h1.close()
h2.close()

EOF
