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

# Test the --tls-priority flag.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_tls_certificates
requires_nbdinfo

# RHEL 8 libnbd / nbdinfo doesn't support the tls-certificates
# parameter in URIs, so connections always fail.  It's hard to detect
# if libnbd supports this, so just go off version number.  The libnbd
# commit adding this feature was 847e0b9830, added in libnbd 1.9.5.
requires_libnbd_version 1.10

out="tls-priority.out"
rm -f $out
cleanup_fn rm -f $out

nbdkit --tls=require --tls-certificates="$pkidir" \
       --tls-priority=NORMAL \
       -D nbdkit.tls.session=1 \
       -v \
       example1 \
       --run '
       # Run nbdinfo against the server.
       nbdinfo "$uri"
       ' > $out 2>&1

cat $out

grep 'TLS: priority string: NORMAL' $out
grep 'is_read_only: true' $out
grep -E 'export-size: 104857600\b' $out
