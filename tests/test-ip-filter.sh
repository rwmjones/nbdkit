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

# Test the ip filter.  Necessarily this is rather limited because we
# are only able to use the loopback connection.

source ./functions.sh
set -e
set -x
set -u

requires_run
requires_nbdinfo
requires_ipv6_loopback

# nbdinfo in RHEL 8 lacks a fix to libxml for parsing
# "nbd://[::1]:$port" URIs (commit 17df436cea5 added in 1.7.7).
requires_libnbd_version 1.8

fail=0

# Allow IPv4 and IPv6.
nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=allipv4,allipv6 deny=all \
       --run '
    nbdinfo "nbd://127.0.0.1:$port"
    nbdinfo "nbd://[::1]:$port"
'

# Alternative using 'loopback'.
nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=loopback deny=all \
       --run '
    nbdinfo "nbd://127.0.0.1:$port"
    nbdinfo "nbd://[::1]:$port"
'

# Alternative using 'loopbackv4' and 'loopbackv6'.
nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=loopbackv4,loopbackv6 deny=all \
       --run '
    nbdinfo "nbd://127.0.0.1:$port"
    nbdinfo "nbd://[::1]:$port"
'

# Allow IPv4 only, deny IPv6.
# XXX We cannot test this because $port is set preferentially to the
# IPv6 port, and we cannot find the IPv4 port here.
#nbdkit -v --port=0 --filter=ip null \
#       -D ip.rules=1 \
#       allow=allipv4 deny=all --run 'nbdinfo "nbd://127.0.0.1:$port"'
#
#nbdkit -v --port=0 --filter=ip null \
#       -D ip.rules=1 \
#       allow=allipv4 deny=all --run 'nbdinfo "nbd://[::1]:$port"' && fail=1

# Allow IPv6 only, deny IPv4.
nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=allipv6 deny=all \
       --run 'nbdinfo "nbd://[::1]:$port"'

nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=loopbackv6 deny=all \
       --run 'nbdinfo "nbd://[::1]:$port"'

nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=allipv6 deny=all \
       --run 'nbdinfo "nbd://127.0.0.1:$port"' && fail=1

nbdkit -v --port=0 --filter=ip null \
       -D ip.rules=1 \
       allow=loopbackv6 deny=loopbackv4 \
       --run 'nbdinfo "nbd://127.0.0.1:$port"' && fail=1

exit $fail
