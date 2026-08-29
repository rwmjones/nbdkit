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

# Test the sparse-random plugin with non-standard blocksize.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdinfo

# nbdcopy >= 1.5.9 required for this test.
requires_nbdcopy
requires_libnbd_version 1.5.9

out=sparse-random-blocksize.out
cleanup_fn rm -f $out
rm -f $out

#blocksize=65536

# We could do this:
#blocksize_r="$(( 10 + (RANDOM % 19) ))"  ;# 10..28
# but if this picks a 32M block size, then this could consume up to
# 4 * 16 * 32 == 2048 MB of RAM.  This is a problem on smaller systems
# (and especially 32 bit), so choose a lesser maximum.
blocksize_r="$(( 10 + (RANDOM % 15) ))"   ;# 10..24
blocksize="$(( 1 << blocksize_r ))"

export out
nbdkit -v \
       sparse-random \
       size=10G blocksize=$blocksize \
       --run '
    nbdinfo --map "$uri" > $out &&
    nbdcopy "$uri" "$uri"
'

# Check all the extents are aligned to $blocksize
cat $out
( while read offset size rest ; do
      echo checking $offset $size ...
      if test "$(( $offset % blocksize ))" != 0 ||
         test "$(( $size % blocksize ))" != 0 ; then
          echo error: extent is not aligned to $blocksize: offset $offset size $size
          exit 1
      fi
done ) <$out
