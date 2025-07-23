#!/usr/bin/env bash
# nbdkit
# Copyright (C) 2025 Shasheen Ediriweera
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

requires_run
requires dd --version
requires_nbdcopy
requires_nbdsh_uri
requires $PYTHON --version

dir=$(mktemp -d /tmp/nbdkit-test-dir.XXXXXX)
sock="$dir/nbdkit-indexed-gzip-test-sock"
pid_file="$dir/nbdkit-indexed-gzip-test.pid"
cleanup_fn rm -rf $dir

create_test_input_files() {
    SIZE_IN_BYTE=$1
    FILENAME=$2

    # Use Python to generate binary data (with a fixed seed for reproducibility), redirected to file
    # Note: random data isn't well compressible , so final size may be larger than the original
    $PYTHON -c "import sys,random;random.seed('fixed-seed-for-reproducibility');sys.stdout.buffer.write(bytes(random.getrandbits(8) for _ in range($SIZE_IN_BYTE)))" > $FILENAME
    # Create SHA256 checksum, taking care to read it from stdin so there's no filename associated in the checksum file
    cat "${FILENAME}" | sha256sum --binary > "${FILENAME}.sha256sum"

    # Compress the input
    gzip --keep $FILENAME
}

# Sequentially dump the contents of the NBD URI, and validate all the contents are identical
assert_end_to_end_decompressed_data_identical() {
    FILENAME=$1
    echo "RUNNING THE END-TO-END TEST"
    nbdkit --filter=indexed-gzip file \
       file="${FILENAME}.gz" \
       gzip-index-path="${FILENAME}.gzi" \
       --run "nbdcopy \$uri ${FILENAME}.nbdcopy"
       cat "${FILENAME}.nbdcopy" | sha256sum --check ${FILENAME}.sha256sum
}

# This test reads various ranges from the file and validates that the decompressed data is identical
assert_random_access_decompressed_data_identical() {
    FILENAME=$1
    BLOCK_SIZE=$2
    SKIP_IN_BLOCKS=$3
    COUNT_IN_BLOCKS=$4

    echo "Testing random access on file $FILENAME with block size $BLOCK_SIZE and skip blocks $SKIP_IN_BLOCKS and count blocks $COUNT_IN_BLOCKS"

    original_file_prefix="${FILENAME}.bs_$BLOCK_SIZE.skip_$SKIP_IN_BLOCKS"

    original_file_hexdump="${original_file_prefix}.hexdump"

    # Original file
    dd if=$FILENAME bs=$BLOCK_SIZE count=$COUNT_IN_BLOCKS skip=$SKIP_IN_BLOCKS | hexdump -C > $original_file_hexdump

    # Variables to pass into nbdsh Python script (as env var)
    export IGZ_OUTPUT_FILENAME="${original_file_prefix}.indexed-gzip"
    export COUNT_IN_BYTES="$(( COUNT_IN_BLOCKS * BLOCK_SIZE ))"
    export OFFSET_IN_BYTES="$(( SKIP_IN_BLOCKS * BLOCK_SIZE ))"

    # Create a bash variable containing a Python script that 'nbdsh' will utilize to do random-access reads
    define script <<'EOF'
import os
import sys

print("Running indexed-gzip nbdsh testing script")

# Get commands from environment variables
IGZ_OUTPUT_FILENAME=os.getenv("IGZ_OUTPUT_FILENAME")
COUNT_IN_BYTES=os.getenv("COUNT_IN_BYTES")
OFFSET_IN_BYTES=os.getenv("OFFSET_IN_BYTES")
if len(IGZ_OUTPUT_FILENAME) == 0 or len(COUNT_IN_BYTES) == 0 or len(OFFSET_IN_BYTES) == 0:
    print("Unable to validate input environment variables")
    sys.exit(1)

def write_bytes(queried_bytes):
   print(f"Writing {len(queried_bytes)} to {IGZ_OUTPUT_FILENAME}")
   with open(IGZ_OUTPUT_FILENAME, "wb") as f:
       f.write(queried_bytes)
       f.flush()

print(f"Reading {int(COUNT_IN_BYTES)} from {int(OFFSET_IN_BYTES)}")
extracted_binary_data = h.pread(count=int(COUNT_IN_BYTES), offset=int(OFFSET_IN_BYTES))
write_bytes(extracted_binary_data)

EOF
    export script

    start_nbdkit \
       -P $pid_file \
       -U $sock \
       --filter=indexed-gzip file \
       file="${FILENAME}.gz" \
       gzip-index-path="${FILENAME}.gzi" \
       --run ' nbdsh -u "$uri" -c "$script" '
    rm -f $sock $pid_file

    # Create the ASCII hexdump from the extracted binary data
    indexed_gzip_file_hexdump="${IGZ_OUTPUT_FILENAME}.hexdump"
    cat $IGZ_OUTPUT_FILENAME | hexdump -C > $indexed_gzip_file_hexdump

    # Compare the two hexdumps
    diff $original_file_hexdump $indexed_gzip_file_hexdump
    if [ $? -eq 0 ]; then
        echo "Successfully validated no differences for file $FILENAME with block size $BLOCK_SIZE and skip blocks $SKIP_IN_BLOCKS and count blocks $COUNT_IN_BLOCKS"
    else
        # Disable set -e to see this
        echo "Differences found for file $FILENAME with block size $BLOCK_SIZE and skip blocks $SKIP_IN_BLOCKS and count blocks $COUNT_IN_BLOCKS"
        exit 1
    fi
}

run_test() {
    SIZE_IN_BYTES=$1
    FILENAME=$2

    create_test_input_files "$SIZE_IN_BYTES" "$FILENAME"
    assert_end_to_end_decompressed_data_identical "$FILENAME"

    # Run through battery of tests of different ranges
    # First block (1024 bytes) with 1 1024 block
    assert_random_access_decompressed_data_identical "$FILENAME" 1024 0 1

    # Various ranges in middle of file
    assert_random_access_decompressed_data_identical "$FILENAME" 1024 10 2
    assert_random_access_decompressed_data_identical "$FILENAME" 1024 100 3
    assert_random_access_decompressed_data_identical "$FILENAME" 1024 1000 4
    assert_random_access_decompressed_data_identical "$FILENAME" 1024 10000 5

    # Second last block (512 bytes)
    assert_random_access_decompressed_data_identical "$FILENAME" 512 $(( ($SIZE_IN_BYTES - 1024) / 512 )) 1
    # Final block. Note for the test that's not block aligned, this will be less than the 512 byte block size
    assert_random_access_decompressed_data_identical "$FILENAME" 512 $(( ($SIZE_IN_BYTES - 512) / 512 )) 1
}

run_test "$((1024 * 1024 * 20))" "$dir/20MiB.img"
run_test "$((1024 * 1024 * 20 - 12345))" "$dir/slightly-less-than-20MiB.img"
