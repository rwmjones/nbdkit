/* zran.h -- example of deflated stream indexing and random access
 * Copyright (C) 2005, 2012, 2018, 2023, 2024 Mark Adler
 * Copyright (C) 2025 Shasheen Ediriweera
 * For conditions of distribution and use, see copyright notice in zlib.h
 * Version 1.5  4 Feb 2024  Mark Adler */

#ifndef ZRAN_H
#define ZRAN_H

#include <stdio.h>
#include "zlib.h"

#define WINSIZE 32768U      // sliding window size
#define CHUNK 16384         // file input buffer size

// decompression modes. these are the inflateinit2() windowbits parameter.
#define RAW -15
#define ZLIB 15
#define GZIP 31

// Access point.
typedef struct point {
    off_t out;          // offset in uncompressed data
    off_t in;           // offset in compressed file of first full byte
    int bits;           // 0, or number of bits (1-7) from byte at in-1
    unsigned dict;      // number of bytes in window to use as a dictionary
    unsigned char *window;  // preceding 32K (or less) of uncompressed data
} point_t;

// Access point list.
struct deflate_index {
    int have;           // number of access points in list
    int mode;           // -15 for raw, 15 for zlib, or 31 for gzip
    off_t length;       // total length of uncompressed data
    point_t *list;      // allocated list of access points
    z_stream strm;      // re-usable inflate engine for extraction
};

// Deallocate an index built by deflate_index_build().
void deflate_index_free(struct deflate_index *index);

// Serialize a deflate_index to a file. Returns 0 on success, or a negative
// error code on failure. The z_stream state is not serialized and will need
// to be reinitialized when the index is deserialized.
//
// Index file created with raw fwrite(), so serialized index tied to system
// endianness
int deflate_index_serialize(struct deflate_index *index, FILE *out);

// Deserialize a deflate_index from a file. Returns a pointer to the
// deserialized index on success, or NULL on failure. The z_stream state
// is reinitialized but not set up for any particular mode - this will be
// done automatically when deflate_index_extract() is called.
struct deflate_index *deflate_index_deserialize(FILE *in);

/*
* Internal management functions
*/

// Add an access point to the list. If out of memory, deallocate the existing
// list and return NULL. index->mode is temporarily the allocated number of
// access points, until it is time for deflate_index_build() to return. Then
// index->mode is set to the mode of inflation.
struct deflate_index *add_point(struct deflate_index *index, off_t in,
    off_t out, off_t beg,
    unsigned char *window);


#ifdef NOPRIME
// Support zlib versions before 1.2.3 (July 2005), or incomplete zlib clones
// that do not have inflatePrime().

#  define INFLATEPRIME inflatePreface

// Append the low bits bits of value to in[] at bit position *have, updating
// *have. value must be zero above its low bits bits. bits must be positive.
// This assumes that any bits above the *have bits in the last byte are zeros.
// That assumption is preserved on return, as any bits above *have + bits in
// the last byte written will be set to zeros.
static inline void append_bits(unsigned value, int bits,
                               unsigned char *in, int *have) {
    in += *have >> 3;           // where the first bits from value will go
    int k = *have & 7;          // the number of bits already there
    *have += bits;
    if (k)
        *in |= value << k;      // write value above the low k bits
    else
        *in = value;
    k = 8 - k;                  // the number of bits just appended
    while (bits > k) {
        value >>= k;            // drop the bits appended
        bits -= k;
        k = 8;                  // now at a byte boundary
        *++in = value;
    }
}

// Insert enough bits in the form of empty deflate blocks in front of the
// low bits bits of value, in order to bring the sequence to a byte boundary.
// Then feed that to inflate(). This does what inflatePrime() does, except that
// a negative value of bits is not supported. bits must be in 0..16. If the
// arguments are invalid, Z_STREAM_ERROR is returned. Otherwise the return
// value from inflate() is returned.
static int inflatePreface(z_stream *strm, int bits, int value) {
    // Check input.
    if (strm == Z_NULL || bits < 0 || bits > 16)
        return Z_STREAM_ERROR;
    if (bits == 0)
        return Z_OK;
    value &= (2 << (bits - 1)) - 1;

    // An empty dynamic block with an odd number of bits (95). The high bit of
    // the last byte is unused.
    static const unsigned char dyn[] = {
        4, 0xe0, 0x81, 8, 0, 0, 0, 0, 0x20, 0xa8, 0xab, 0x1f
    };
    const int dynlen = 95;          // number of bits in the block

    // Build an input buffer for inflate that is a multiple of eight bits in
    // length, and that ends with the low bits bits of value.
    unsigned char in[(dynlen + 3 * 10 + 16 + 7) / 8];
    int have = 0;
    if (bits & 1) {
        // Insert an empty dynamic block to get to an odd number of bits, so
        // when bits bits from value are appended, we are at an even number of
        // bits.
        memcpy(in, dyn, sizeof(dyn));
        have = dynlen;
    }
    while ((have + bits) & 7)
        // Insert empty fixed blocks until appending bits bits would put us on
        // a byte boundary. This will insert at most three fixed blocks.
        append_bits(2, 10, in, &have);

    // Append the bits bits from value, which takes us to a byte boundary.
    append_bits(value, bits, in, &have);

    // Deliver the input to inflate(). There is no output space provided, but
    // inflate() can't get stuck waiting on output not ingesting all of the
    // provided input. The reason is that there will be at most 16 bits of
    // input from value after the empty deflate blocks (which themselves
    // generate no output). At least ten bits are needed to generate the first
    // output byte from a fixed block. The last two bytes of the buffer have to
    // be ingested in order to get ten bits, which is the most that value can
    // occupy.
    strm->avail_in = have >> 3;
    strm->next_in = in;
    strm->avail_out = 0;
    strm->next_out = in;                // not used, but can't be NULL
    return inflate(strm, Z_NO_FLUSH);
}

#else
#  define INFLATEPRIME inflatePrime
#endif

#endif /* ZRAN_H */
