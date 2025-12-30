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

# Test checkwrite + evil filters together.

source ./functions.sh
set -e
set -x
set -u

# Across a 1G disk, with the default failure probability of the evil
# filter, we expect about 43 stuck high bits.  The test actually fails
# at the first stuck high bit so we don't end up grinding across the
# whole virtual size.
size=1G

requires_plugin null
requires_filter checkwrite
requires_filter evil
requires_filter noextents

# nbdcopy >= 1.5.9 required for this test.
requires_nbdcopy
requires_libnbd_version 1.5.9

sock1=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
sock2=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
pid1=checkwrite-evil.pid1
pid2=checkwrite-evil.pid2
files="$sock1 $sock2 $pid1 $pid2"
rm -f $files
cleanup_fn rm -f $files

# Source nbdkit.
start_nbdkit -P $pid1 -U $sock1 \
             null $size \
             --filter=evil \
             --filter=noextents

# Target nbdkit.
start_nbdkit -P $pid2 -U $sock2 \
             -v -D checkwrite.showdiffs=1 \
             null $size \
             --filter=checkwrite

fail=
nbdcopy "nbd+unix:///?socket=$sock1" "nbd+unix:///?socket=$sock2" || fail=1
if ! test "$fail"; then
    echo "$0: expected nbdcopy to fail but it did not"
    exit 1
fi
