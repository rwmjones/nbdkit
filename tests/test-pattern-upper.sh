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

# Test the pattern plugin with upper=N parameter.

source ./functions.sh
set -e
set -x
set -u

# This won't work on Windows because systemd socket activation
# isn't supported.
if is_windows; then
   echo "$0: systemd socket activation not supported on Windows"
   exit 77
fi

requires hexdump --version

requires nbdsh --version
# Requires explicit h.close() method, added in 1.16
requires_libnbd_version 1.16

nbdsh -c - <<'EOF'
import random
import subprocess

random.seed(None)

size = 4194319  # a prime > 4M
upper = random.randint(0,65535)

expected = bytearray()
offset = 0
while len(expected) < size:
    d = upper << 48 ^ offset
    expected.extend(d.to_bytes(8, 'big'))
    offset = offset + 8

h.connect_systemd_socket_activation([
    "nbdkit", "--exit-with-parent", "pattern",
    str(size), "upper=" + str(upper)
])

for iter in range(1,100):
    offset = random.randint(0, size)
    count = min(size-offset, random.randint(1, 50))
    if count > 0:
        buf = h.pread(count, offset)
        if buf != expected[offset:offset+count]:
            print("FAIL: mismatch between actual and expected data",
                  file=sys.stderr, flush=True)
            print("actual:", flush=True)
            cmd = subprocess.Popen(["hexdump", "-C"], stdin=subprocess.PIPE)
            cmd.stdin.write(buf)
            cmd.stdin.close()
            cmd.wait()
            print("expected:", flush=True)
            cmd = subprocess.Popen(["hexdump", "-C"], stdin=subprocess.PIPE)
            cmd.stdin.write(expected[offset:offset+count])
            cmd.stdin.close()
            cmd.wait()
            exit(1)

h.shutdown()
h.close()

EOF
