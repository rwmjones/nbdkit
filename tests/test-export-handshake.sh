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

requires_plugin sh
requires nbdsh -c 'print(h.set_request_meta_context)'

export sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
files="$sock export-handshake.pid"
rm -f $files
cleanup_fn rm -f $files

# Create an nbdkit sh plugin for checking correct OPT_SET_META_CONTEXT handling
define plugin <<'EOF'
case "$1" in
    open) echo "$3" ;;
    get_size) echo "$2" | wc -c ;;
    can_extents) exit 0 ;;
    extents) echo $4 $3 ;;
    *) exit 2 ;;
esac
EOF

start_nbdkit -P export-handshake.pid -U $sock sh - <<<"$plugin"

nbdsh -c '
import os

def must_fail(f, *args, **kwds):
  try:
    f(*args, **kwds)
    assert False
  except nbd.Error:
    pass

def check_context(name):
  assert name == nbd.CONTEXT_BASE_ALLOCATION

def check_extents(name, offset, extents, err):
  assert name == nbd.CONTEXT_BASE_ALLOCATION

# Test 0: Baseline: Successful base:allocation negotiation with manual handshake

# Drive handshake manually, rather than relying on usual libnbd sequence
# However, structured replies should already be negotiated
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("zero")
h.add_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
assert h.opt_set_meta_context(check_context) == 1
assert h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
assert h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.block_status(1, 0, check_extents)
h.shutdown()

# Test 1: OPT_GO without SET_META_CONTEXT

h = nbd.NBD()
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("one")
h.add_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.block_status, 1, 0, check_extents)
h.shutdown()

# Test 2: SET_META_CONTEXT with no context requested

h = nbd.NBD()
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("two")
assert h.opt_set_meta_context(check_context) == 0
assert not h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
assert not h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.block_status, 1, 0, check_extents)
h.shutdown()

# Test 3: changing export name wipes out negotiated context

h = nbd.NBD()
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("three.1")
h.add_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
assert h.opt_set_meta_context(check_context) == 1
assert h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.set_export_name("three.2")
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.block_status, 1, 0, check_extents)
h.shutdown()

# Test 4: listing contexts does not set context

h = nbd.NBD()
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("four")
h.add_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
assert h.opt_list_meta_context(check_context) == 1
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
must_fail(h.can_meta_context, nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.block_status, 1, 0, check_extents)
h.shutdown()

# Test 5: a later SET_META_CONTEXT overrides an earlier one

h = nbd.NBD()
h.set_request_meta_context(False)
h.set_strict_mode(0)
h.set_opt_mode(True)
h.connect_unix(os.environ["sock"])
assert h.get_structured_replies_negotiated()

h.set_export_name("five")
h.add_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
assert h.opt_set_meta_context(check_context) == 1
assert h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.clear_meta_contexts()
assert h.opt_set_meta_context(check_context) == 0
assert not h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
h.opt_go()
assert not h.can_meta_context(nbd.CONTEXT_BASE_ALLOCATION)
must_fail(h.block_status, 1, 0, check_extents)
h.shutdown()
'
