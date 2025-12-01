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
requires_plugin eval
requires_nbdsh_uri
requires dd --version

f=test-fua-flush-on-close.mark
rm -f $f
cleanup_fn rm -f $f

# flush-on-close=false should NOT create the sentinel file.
nbdkit -fv eval \
       get_size="echo 512" \
       flush="echo flushed > $f" \
       pwrite="dd of=/dev/null" \
       --filter=fua \
       fua-mode=pass \
       --run 'nbdsh -u "$uri" -c "h.pwrite(bytearray(512), 0)"'

! test -f $f

# flush-on-close=true SHOULD create the sentinel file.
nbdkit -fv eval \
       get_size="echo 512" \
       flush="echo flushed > $f" \
       pwrite="dd of=/dev/null" \
       --filter=fua \
       fua-mode=pass flush-on-close=true \
       --run 'nbdsh -u "$uri" -c "h.pwrite(bytearray(512), 0)"'

# The flush method must have been called.
grep flushed $f
