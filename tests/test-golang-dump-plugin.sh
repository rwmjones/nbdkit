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

skip_if_valgrind "because Golang code leaks memory"

# Run --dump-plugin on each example, which is enough to check the
# plugin has been compiled properly and loads.

for f in ../plugins/golang/examples/*/nbdkit-*-plugin.so; do
    if test -f $f; then
        nbdkit -f -v $f --dump-plugin
    fi
done

# Check for the special output and other fields from dump-plugin.

plugin=../plugins/golang/examples/dump-plugin/nbdkit-godump-plugin.so
requires test -r $plugin

out=golang-dump-plugin.out
rm -f $out
cleanup_fn rm -f $out

nbdkit -f $plugin --dump-plugin > $out
cat $out
grep "^name=dumpplugin$" $out
grep "^has_dump_plugin=1$" $out
grep "^golang_dump_plugin=1$" $out
