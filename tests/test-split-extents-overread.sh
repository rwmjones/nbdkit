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

# Regression test for an earlier crash in split plugin extents
# processing.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdsh_uri
requires nbdsh --base-allocation --version
requires $TRUNCATE --help
requires $STAT --help

f1=test-split-extents-overread.1
f2=test-split-extents-overread.2
files="$f1 $f2"
rm -f $files
cleanup_fn rm -f $files

# Create allocated and sparse files.
dd if=/dev/zero of=$f1 bs=1024k count=8
truncate -s $((1024*1024)) $f2

define script <<'EOF'
def cb(metacontext, offset, e, err):
    pass
h.block_status(4096, 0, cb) # no CMD_FLAG_REQ_ONE
EOF
export script

# This crashed in earlier versions.
nbdkit -v split $f1 $f2 \
       --run ' nbdsh --base-allocation --uri "$uri" -c "$script" '
