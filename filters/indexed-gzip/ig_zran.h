/* ig_zran.h -- indexed gzip customizations of zran example for use in nbdkit
 * Copyright (C) 2005, 2012, 2018, 2023, 2024 Mark Adler
 * Copyright (C) 2025 Shasheen Ediriweera
 * For conditions of distribution and use, see copyright notice in zlib.h
 * Version 1.6  2 Aug 2024  Mark Adler */

#ifndef IG_ZRAN_H
#define IG_ZRAN_H

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>

#include <zlib.h>

#include <nbdkit-filter.h>

#include "cleanup.h"
#include "pread.h"
#include "minmax.h"

#include "ig_handle.h"
#include "zran.h"

// An extension to the return code that's compatible with the sub-zero range defined
// within "zlib.h", (currently [-6, 0])
// This enables the modified "zran.c" functions defined below
// to unambiguously indicate to the caller that nbdkit_err field has been filled and must be handled,
// without clobbering the ZLIB errors that the function can also return
#define Z_NBDKIT_ERROR (-99)

// Performs almost identical functionality as zran's deflate_index_build(),
// but conducts I/O operations using the nbdkit pread() function, rather
// than system calls on the FILE pointer.
// Follows the NBDkit pread() function signature, as the handle needs to be passed to nbdkit subcalls.
//
// Additionally returns Z_NBDKIT_ERROR when the nbdkit_err has been set.
//
// Here's the original documentation adapted from "zran.h":
//
// Make one pass through a zlib, gzip, or raw deflate compressed stream and
// build an index, with access points about every span bytes of uncompressed
// output. gzip files with multiple members are fully indexed. span should be
// chosen to balance the speed of random access against the memory requirements
// of the list, which is about 32K bytes per access point. The return value is
// the number of access points on success (>= 1), Z_MEM_ERROR for out of
// memory, Z_BUF_ERROR for a premature end of input, Z_DATA_ERROR for a format
// or verification error in the input file, or Z_ERRNO for a file read error.
//
// Custom field of Z_NBDKIT_ERROR set when nbdkit_err has been set, to provide
// caller with unambigious delineation of NBDKit errors from zlib errors
int ig_deflate_index_build(nbdkit_next *next, void* handle, off_t span, int* nbdkit_err);

// Performas almost identical functionality to zran's deflate_index_extract(),
// but conducts I/O operations using the nbdkit pread() function, rather than system
// calls on the FILE pointer.
//
// Follows the NBDkit pread() function signature, as the handle needs to be passed to nbdkit subcalls.
// Additionally returns Z_NBDKIT_ERROR when the nbdkit_err has been set.
//
// Here's the original documentation adapted from "zran.h":
//
// Use the index to read len bytes from offset into buf. Return the number of
// bytes read or a negative error code. If data is requested past the end of
// the uncompressed data, then deflate_index_extract() will return a value less
// than len, indicating how much was actually read into buf. If given a valid
// index, this function should not return an error unless the file was modified
// somehow since the index was generated, given that deflate_index_build() had
// validated all of the input. If nevertheless there is a failure, Z_BUF_ERROR
// is returned if the compressed data ends prematurely, Z_DATA_ERROR if the
// deflate compressed data is not valid, Z_MEM_ERROR if out of memory,
// Z_STREAM_ERROR if the index is not valid, or Z_ERRNO if there is an error
// reading or seeking on the input file.
//
// Custom field of Z_NBDKIT_ERROR set when nbdkit_err has been set, to provide
// caller with unambigious delineation of NBDKit errors from zlib errors
ptrdiff_t ig_deflate_index_extract(nbdkit_next *next, void *handle, off_t offset, unsigned char *buf, size_t len, int* err);

#endif /* IG_ZRAN_H */
