/* zran.c -- example of deflate stream indexing and random access
 * Copyright (C) 2005, 2012, 2018, 2023, 2024 Mark Adler
 * Copyright (C) 2025 Shasheen Ediriweera
 * For conditions of distribution and use, see copyright notice in zlib.h
 * Version 1.6  2 Aug 2024  Mark Adler */

/* Version History:
 1.0  29 May 2005  First version
 1.1  29 Sep 2012  Fix memory reallocation error
 1.2  14 Oct 2018  Handle gzip streams with multiple members
                   Add a header file to facilitate usage in applications
 1.3  18 Feb 2023  Permit raw deflate streams as well as zlib and gzip
                   Permit crossing gzip member boundaries when extracting
                   Support a size_t size when extracting (was an int)
                   Do a binary search over the index for an access point
                   Expose the access point type to enable save and load
 1.4  13 Apr 2023  Add a NOPRIME define to not use inflatePrime()
 1.5   4 Feb 2024  Set returned index to NULL on an index build error
                   Stop decoding once request is satisfied
                   Provide a reusable inflate engine in the index
                   Allocate the dictionaries to reduce memory usage
 1.6   2 Aug 2024  Remove unneeded dependency on limits.h
 */

// Illustrate the use of Z_BLOCK, inflatePrime(), and inflateSetDictionary()
// for random access of a compressed file. A file containing a raw deflate
// stream is provided on the command line. The compressed stream is decoded in
// its entirety, and an index built with access points about every SPAN bytes
// in the uncompressed output. The compressed file is left open, and can then
// be read randomly, having to decompress on the average SPAN/2 uncompressed
// bytes before getting to the desired block of data.
//
// An access point can be created at the start of any deflate block, by saving
// the starting file offset and bit of that block, and the 32K bytes of
// uncompressed data that precede that block. Also the uncompressed offset of
// that block is saved to provide a reference for locating a desired starting
// point in the uncompressed stream. deflate_index_build() decompresses the
// input raw deflate stream a block at a time, and at the end of each block
// decides if enough uncompressed data has gone by to justify the creation of a
// new access point. If so, that point is saved in a data structure that grows
// as needed to accommodate the points.
//
// To use the index, an offset in the uncompressed data is provided, for which
// the latest access point at or preceding that offset is located in the index.
// The input file is positioned to the specified location in the index, and if
// necessary the first few bits of the compressed data is read from the file.
// inflate is initialized with those bits and the 32K of uncompressed data, and
// decompression then proceeds until the desired offset in the file is reached.
// Then decompression continues to read the requested uncompressed data from
// the file.
//
// There is some fair bit of overhead to starting inflation for the random
// access, mainly copying the 32K byte dictionary. If small pieces of the file
// are being accessed, it would make sense to implement a cache to hold some
// lookahead to avoid many calls to deflate_index_extract() for small lengths.
//
// Another way to build an index would be to use inflateCopy(). That would not
// be constrained to have access points at block boundaries, but would require
// more memory per access point, and could not be saved to a file due to the
// use of pointers in the state. The approach here allows for storage of the
// index in a file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "zran.h"

// See comments in zran.h.
void deflate_index_free(struct deflate_index *index) {
    if (index != NULL) {
        size_t i = index->have;
        while (i)
            free(index->list[--i].window);
        free(index->list);
        inflateEnd(&index->strm);
        free(index);
    }
}

// Add an access point to the list. If out of memory, deallocate the existing
// list and return NULL. index->mode is temporarily the allocated number of
// access points, until it is time for deflate_index_build() to return. Then
// index->mode is set to the mode of inflation.
struct deflate_index *add_point(struct deflate_index *index, off_t in,
                                 off_t out, off_t beg,
                                 unsigned char *window) {
    if (index->have == index->mode) {
        // The list is full. Make it bigger.
        index->mode = index->mode ? index->mode << 1 : 8;
        point_t *next = realloc(index->list, sizeof(point_t) * index->mode);
        if (next == NULL) {
            deflate_index_free(index);
            return NULL;
        }
        index->list = next;
    }

    // Fill in the access point and increment how many we have.
    point_t *next = (point_t *)(index->list) + index->have++;
    if (index->have < 0) {
        // Overflowed the int!
        deflate_index_free(index);
        return NULL;
    }
    next->out = out;
    next->in = in;
    next->bits = index->strm.data_type & 7;
    next->dict = out - beg > WINSIZE ? WINSIZE : (unsigned)(out - beg);
    next->window = malloc(next->dict);
    if (next->window == NULL) {
        deflate_index_free(index);
        return NULL;
    }
    unsigned recent = WINSIZE - index->strm.avail_out;
    unsigned copy = recent > next->dict ? next->dict : recent;
    memcpy(next->window + next->dict - copy, window + recent - copy, copy);
    copy = next->dict - copy;
    memcpy(next->window, window + WINSIZE - copy, copy);

    // Return the index, which may have been newly allocated or destroyed.
    return index;
}

// See comments in zran.h.
int deflate_index_serialize(struct deflate_index *index, FILE *out) {
    if (index == NULL || out == NULL)
        return Z_STREAM_ERROR;

    // Write the header: have, mode, length
    if (fwrite(&index->have, sizeof(int), 1, out) != 1 ||
        fwrite(&index->mode, sizeof(int), 1, out) != 1 ||
        fwrite(&index->length, sizeof(off_t), 1, out) != 1)
        return Z_ERRNO;

    // Write each access point
    for (int i = 0; i < index->have; i++) {
        point_t *point = &index->list[i];

        // Write the point metadata
        if (fwrite(&point->out, sizeof(off_t), 1, out) != 1 ||
            fwrite(&point->in, sizeof(off_t), 1, out) != 1 ||
            fwrite(&point->bits, sizeof(int), 1, out) != 1 ||
            fwrite(&point->dict, sizeof(unsigned), 1, out) != 1)
            return Z_ERRNO;

        // Write the window data
        if (point->dict > 0 && point->window != NULL) {
            if (fwrite(point->window, 1, point->dict, out) != point->dict)
                return Z_ERRNO;
        }
    }

    return Z_OK;
}

// See comments in zran.h.
struct deflate_index *deflate_index_deserialize(FILE *in) {
    if (in == NULL)
        return NULL;

    // Allocate the index structure
    struct deflate_index *index = malloc(sizeof(struct deflate_index));
    if (index == NULL)
        return NULL;

    // Initialize the z_stream state - this will be properly set up
    // when deflate_index_extract() is first called
    index->strm.zalloc = Z_NULL;
    index->strm.zfree = Z_NULL;
    index->strm.opaque = Z_NULL;
    index->strm.avail_in = 0;
    index->strm.avail_out = 0;

    // Initialize the inflate state for the deserialized index
    int ret = inflateInit2(&index->strm, RAW);
    if (ret != Z_OK) {
        free(index);
        return NULL;
    }

    // Read the header: have, mode, length
    if (fread(&index->have, sizeof(int), 1, in) != 1 ||
        fread(&index->mode, sizeof(int), 1, in) != 1 ||
        fread(&index->length, sizeof(off_t), 1, in) != 1) {
        free(index);
        return NULL;
    }

    // Validate the header values
    if (index->have < 0 || index->have > 1000000 ||  // reasonable upper limit
        (index->mode != RAW && index->mode != ZLIB && index->mode != GZIP) ||
        index->length < 0) {
        free(index);
        return NULL;
    }

    // Allocate the access points list
    if (index->have == 0) {
        index->list = NULL;
    } else {
        index->list = malloc(sizeof(point_t) * index->have);
        if (index->list == NULL) {
            free(index);
            return NULL;
        }
    }

    // Read each access point
    for (int i = 0; i < index->have; i++) {
        point_t *point = &index->list[i];

        // Read the point metadata
        if (fread(&point->out, sizeof(off_t), 1, in) != 1 ||
            fread(&point->in, sizeof(off_t), 1, in) != 1 ||
            fread(&point->bits, sizeof(int), 1, in) != 1 ||
            fread(&point->dict, sizeof(unsigned), 1, in) != 1) {
            // Clean up partial allocation
            for (int j = 0; j < i; j++)
                free(index->list[j].window);
            free(index->list);
            free(index);
            return NULL;
        }

        // Validate point data
        if (point->out < 0 || point->in < 0 ||
            point->bits < 0 || point->bits > 7 ||
            point->dict > WINSIZE) {
            // Clean up partial allocation
            for (int j = 0; j < i; j++)
                free(index->list[j].window);
            free(index->list);
            free(index);
            return NULL;
        }

        // Read the window data
        if (point->dict > 0) {
            point->window = malloc(point->dict);
            if (point->window == NULL) {
                // Clean up partial allocation
                for (int j = 0; j < i; j++)
                    free(index->list[j].window);
                free(index->list);
                free(index);
                return NULL;
            }

            if (fread(point->window, 1, point->dict, in) != point->dict) {
                // Clean up partial allocation
                free(point->window);
                for (int j = 0; j < i; j++)
                    free(index->list[j].window);
                free(index->list);
                free(index);
                return NULL;
            }
        } else {
            point->window = NULL;
        }
    }

    return index;
}

#ifdef TEST

#define SPAN 1048576L       // desired distance between access points
#define LEN 16384           // number of bytes to extract

// Demonstrate the use of deflate_index_build() and deflate_index_extract() by
// processing the file provided on the command line, and extracting LEN bytes
// from 2/3rds of the way through the uncompressed output, writing that to
// stdout. An offset can be provided as the second argument, in which case the
// data is extracted from there instead.
int main(int argc, char **argv) {
    // Open the input file.
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: zran file.raw [offset] [index_file]\n");
        fprintf(stderr, "  If index_file exists, it will be loaded.\n");
        fprintf(stderr, "  If index_file doesn't exist, an index will be built and saved.\n");
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "zran: could not open %s for reading\n", argv[1]);
        return 1;
    }

    // Get optional offset.
    off_t offset = -1;
    if (argc >= 3) {
        char *end;
        offset = strtoll(argv[2], &end, 10);
        if (*end || offset < 0) {
            fprintf(stderr, "zran: %s is not a valid offset\n", argv[2]);
            return 1;
        }
    }

    // Get optional index file.
    struct deflate_index *index = NULL;
    const char *index_file = (argc == 4) ? argv[3] : NULL;

    if (index_file) {
        // Try to load index from file first.
        FILE *idx_in = fopen(index_file, "rb");
        if (idx_in != NULL) {
            index = deflate_index_deserialize(idx_in);
            fclose(idx_in);
            if (index != NULL) {
                fprintf(stderr, "zran: loaded index from %s\n", index_file);
            } else {
                fprintf(stderr, "zran: failed to load index from %s, will rebuild\n", index_file);
            }
        }
    }

    if (index == NULL) {
        // Build index.
        int len = deflate_index_build(in, SPAN, &index);
        if (len < 0) {
            fclose(in);
            switch (len) {
            case Z_MEM_ERROR:
                fprintf(stderr, "zran: out of memory\n");
                break;
            case Z_BUF_ERROR:
                fprintf(stderr, "zran: %s ended prematurely\n", argv[1]);
                break;
            case Z_DATA_ERROR:
                fprintf(stderr, "zran: compressed data error in %s\n", argv[1]);
                break;
            case Z_ERRNO:
                fprintf(stderr, "zran: read error on %s\n", argv[1]);
                break;
            default:
                fprintf(stderr, "zran: error %d while building index\n", len);
            }
            return 1;
        }
        fprintf(stderr, "zran: built index with %d access points\n", len);

        // Save index to file if filename provided
        if (index_file) {
            FILE *idx_out = fopen(index_file, "wb");
            if (idx_out != NULL) {
                int ret = deflate_index_serialize(index, idx_out);
                fclose(idx_out);
                if (ret == Z_OK) {
                    fprintf(stderr, "zran: saved index to %s\n", index_file);
                } else {
                    fprintf(stderr, "zran: failed to save index to %s\n", index_file);
                }
            } else {
                fprintf(stderr, "zran: could not create %s for writing\n", index_file);
            }
        }
    }

    // Use index by reading some bytes from an arbitrary offset.
    unsigned char buf[LEN];
    if (offset == -1)
        offset = ((index->length + 1) << 1) / 3;
    ptrdiff_t got = deflate_index_extract(in, index, offset, buf, LEN);
    if (got < 0)
        fprintf(stderr, "zran: extraction failed: %s error\n",
                got == Z_MEM_ERROR ? "out of memory" : "input corrupted");
    else {
        fwrite(buf, 1, got, stdout);
        fprintf(stderr, "zran: extracted %ld bytes at %lld\n", got, offset);
    }

    // Clean up and exit.
    deflate_index_free(index);
    fclose(in);
    return 0;
}

#endif
