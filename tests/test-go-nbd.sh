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

# Check that go-nbd (https://github.com/digitalocean/go-nbd) can
# interoperate with nbdkit.
#
# That project has its own interoperability test, so this test is
# mainly concerned with ensuring we don't regress in nbdkit.

source ./functions.sh
set -e
set -x
#set -u

d="$srcdir/test-go-nbd.d"
requires test -d "$d"

requires_plugin memory
requires $CUT --version

requires test -n "$GOLANG"
requires test "$GOLANG" != "no"
requires "$GOLANG" version

# Check minimum golang version (see $d/go.mod):
min_version="$( grep "^go 1\." "$d/go.mod" | $CUT -d . -f 2 )"
this_version="$( "$GOLANG" version | grep -Eo 'go1\.[0-9]+' | $CUT -d . -f 2 )"

requires test "$this_version" -ge "$min_version"

(cd "$d" && "$GOLANG" test -v)
