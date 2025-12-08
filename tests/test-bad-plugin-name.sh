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

# Test bad plugin names and the resulting error messages.

source ./functions.sh
set -e
set -x
set -u

out="test-bad-plugin-name.out"
rm -f $out
cleanup_fn rm -f $out

# A name that could exist but does not.
#
# Note here we have to run the server directly because the wrapper
# will rewrite the plugin name.
../server/nbdkit$EXEEXT nosuchname 2>$out ||:
cat $out
grep 'nosuchname' $out
# The error message should suggest a Fedora or Debian package name:
grep 'nbdkit-nosuchname-plugin' $out
grep 'nbdkit-plugin-nosuchname' $out
# The error should mention the manual:
grep -F 'nbdkit(1)' $out

# Valid but unusual short name.
nbdkit "plugin@name" 2>$out ||:
cat $out
grep 'plugin@name' $out
# The error message should NOT suggest a package name:
! grep 'nbdkit-plugin@name-plugin' $out

# Test quoting of output strings (also: test-shebang-crlf.sh)
nbdkit "$(printf 'bad\r')" 2>$out ||:
cat $out
grep 'bad\\r' $out
# The error message should NOT suggest a package name:
! grep 'nbdkit-bad.?-plugin' $out
