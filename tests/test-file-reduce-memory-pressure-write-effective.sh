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

# Test the file plugin, writing with reduce-memory-pressure=true, is
# effective at reducing page cache usage.

source ./functions.sh
set -e
set -x
set -u

# Makes no sense to run this test under valgrind.
skip_if_valgrind

requires_plugin file
requires_run
requires_nbdcopy
requires $TRUNCATE --version
requires test -r /dev/urandom
requires dd --version
requires $SED --version

# Requires the cachestats tool from https://github.com/Feh/nocache
# It doesn't support --version or --help, so use 'type' instead.
requires type cachestats

inp=file-reduce-memory-pressure-write-effective.in
out=file-reduce-memory-pressure-write-effective.out
stats1=file-reduce-memory-pressure-write-effective.s1
stats2=file-reduce-memory-pressure-write-effective.s2
rm -f $inp $out $stats1 $stats2
cleanup_fn rm -f $inp $out $stats1 $stats2

# Create a large random file as input.
dd if=/dev/urandom of=$inp bs=1024k count=1024

# Copy to output using reduce-memory-pressure=false and collect the
# stats.  We expect to see the output file mostly or completely in
# cache after.
rm -f $out; truncate -r $inp $out
export inp
nbdkit file $out --run 'nbdcopy $inp "$uri"' reduce-memory-pressure=false
cachestats $out > $stats1
cat $stats1

# The same, with reduce-memory-pressure=true.
# We expect to see the output file not cached after.
rm -f $out; truncate -r $inp $out
export inp
nbdkit file $out --run 'nbdcopy $inp "$uri"' reduce-memory-pressure=true
cachestats $out > $stats2
cat $stats2

# The output of cachestats looks like this:
# pages in cache: 262144/262144 (100.0%)  [filesize=1048576.0K, pagesize=4K]
# We want to check that % pages in cache using
# reduce-memory-pressure=true is much lower than the default case.
pic1="$($SED 's,pages in cache: [0-9/]* (\([0-9]*\)\.[0-9]*%).*,\1,' \
             < $stats1)"
pic2="$($SED 's,pages in cache: [0-9/]* (\([0-9]*\)\.[0-9]*%).*,\1,' \
             < $stats2)"

# Test before is > 10%
test "$pic1" -gt 10
# Test after is < 10%
test "$pic2" -lt 10
