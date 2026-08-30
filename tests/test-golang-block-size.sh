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

# Test block size.

source ./functions.sh
set -e
set -x
set -u

skip_if_valgrind "because Golang code leaks memory"

requires_nbdinfo

plugin=../plugins/golang/testplugins/blocksize/nbdkit-goblocksize-plugin.so
requires test -r $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
out=golang-block-size.out
pid=golang-block-size.pid
rm -f $out $pid
cleanup_fn rm -f $out $pid $sock

# Choose some random block sizes.  These have to be powers of 2 and
# there are certain other limitations.
minimum=$(( 1 << (RANDOM % 6) ))
preferred=$(( 1 << (10 + (RANDOM % 6)) ))
maximum=$(( 1 << (18 + (RANDOM % 4)) ))

# Golang requires us to run nbdkit in the foreground.
start_nbdkit_foreground -P $pid -U $sock $plugin \
                        minimum=$minimum preferred=$preferred maximum=$maximum

nbdinfo "nbd+unix:///?socket=$sock" > $out
cat $out

grep "block_size_minimum: $minimum" $out
grep "block_size_preferred: $preferred" $out
grep "block_size_maximum: $maximum" $out
