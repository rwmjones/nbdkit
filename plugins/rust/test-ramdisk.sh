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

# The cargo tests use a mock environment which doesn't test the full
# stack.  In particular it doesn't load the plugin into nbdkit and try
# to access it with a client.  This test checks that the ramdisk
# example works.

source ../../tests/functions.sh
set -e

# Not supported on Windows.
if is_windows; then
    echo "$0: test not implemented on Windows"
    exit 77
fi

ramdisk=target/release/examples/libramdisk.so
requires test -x $ramdisk

requires_run
requires_nbdinfo
requires_nbdsh_uri

nbdkit -fv $ramdisk size=10M --run 'nbdinfo "$uri"'

nbdkit -fv $ramdisk size=10M \
       --run '
    nbdsh -u "$uri" \
          -c "buf = b\"1234\"*1024" \
          -c "h.pwrite(buf, 16384)" \
          -c "buf2 = h.pread(4096, 16384)" \
          -c "assert buf == buf2"
'

# If hexdump=true, calling pwrite will run debug_hexdump.  This just
# tests that it doesn't crash, you have to verify the hexdump by
# looking at the test output.
nbdkit -fv $ramdisk size=10M hexdump=true \
       --run '
    nbdsh -u "$uri" \
          -c "buf = b\"1234\"*20" \
          -c "h.pwrite(buf, 0x502)"
'
