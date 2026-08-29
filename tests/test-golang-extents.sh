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

# Test extents support in the golang plugin.

source ./functions.sh
set -e
set -x
set -u

skip_if_valgrind "because Golang code leaks memory"

requires_nbdinfo

plugin=../plugins/golang/testplugins/extents/nbdkit-goextents-plugin.so
requires test -r $plugin

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid=golang-extents.pid
out=golang-extents.out
rm -f $pid
cleanup_fn rm -f $out $pid $sock

# Golang requires us to run nbdkit in the foreground.
start_nbdkit_foreground -P $pid -U $sock $plugin

# Check the output matches the list in the plugin.
nbdinfo --map "nbd+unix:///?socket=$sock" > $out
cat $out

if [ "$(cat $out)" != '         0        1000    0  data
      1000        1000    3  hole,zero
      2000           1    0  data
      2001         999    3  hole,zero
      3000           1    0  data
      3001           1    3  hole,zero
      3002           1    0  data
      3003           1    3  hole,zero
      3004     1045572    0  data' ]; then
    echo "$0: output does not match expected"
    exit 1
fi
