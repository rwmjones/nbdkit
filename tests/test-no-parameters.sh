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

# In the past plugins have crashed when passed no parameters.
#
# Note it's normal for them to give an error (eg. missing parameter)
# we are only looking for crashes here.

source ./functions.sh
#set -e  # no, need return codes below
set -x
set -u

if is_windows; then
    echo "$0: scripts not supported on Windows"
    exit 77
fi

sock=$(mktemp -u /tmp/nbdkit-test-sock.XXXXXX)
rm -f $sock
cleanup_fn rm -f $sock

run_test ()
{
    # What we're doing here is running the plugin with no parameters,
    # but ensuring that nbdkit will shutdown almost immediately.  The
    # exitwhen filter will trigger shutdown in .get_ready().  That is
    # after plugin configuration is done which is the most likely time
    # to see a crash.
    #
    # The socket is never used.  However the alternative '-U -'
    # creates a temporary directory in /tmp which never gets cleaned
    # up along this code path, creating dozens of temporary
    # directories when running the test suite.
    rm -f $sock
    nbdkit -f -v -U $sock --filter=exitwhen $1 exit-when-script="exit 88"
    r=$?
    if [ $r -eq 119 ]; then
        echo "FAIL: valgrind failure in plugin $1"
        exit 1
    elif [ $r -ge 128 ]; then
        echo "FAIL: plugin $1 crashed with signal" $((r-128))
        exit 1
    fi
}

do_test ()
{
    vg=; [ "${NBDKIT_VALGRIND:-0}" = "1" ] && vg="-valgrind"
    case "$1$vg" in
        example4-valgrind | gcs-valgrind | lua-valgrind | \
        python-valgrind | S3-valgrind | tcl-valgrind)
            echo "$0: skipping $1$vg because this language doesn't support valgrind"
            ;;
        example4)
            # This test is written in Perl so we have to check that
            # the Perl plugin was compiled.
            if nbdkit perl --version; then run_test $1; fi
            ;;
	nbd*)
	    # Because of macOS SIP misfeature the DYLD_* environment
	    # variable added by libnbd/run is filtered out and the
	    # test won't work.  Skip it entirely on Macs.
	    if test "$(uname)" != "Darwin"; then run_test $1; fi
	    ;;
        vram*)
            echo "$0: skipping vram to avoid allocating all GPU Video RAM"
            ;;
        *)
            run_test $1
            ;;
    esac
}
foreach_plugin do_test
