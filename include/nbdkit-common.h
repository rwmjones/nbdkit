/* nbdkit
 * Copyright (C) 2013-2018 Red Hat Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef NBDKIT_COMMON_H
#define NBDKIT_COMMON_H

#if !defined (NBDKIT_PLUGIN_H) && !defined (NBDKIT_FILTER_H)
#error this header file should not be directly included
#endif

#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ATTRIBUTE_FORMAT_PRINTF(fmtpos, argpos) \
  __attribute__((__format__ (__printf__, fmtpos, argpos)))
#else
#define ATTRIBUTE_FORMAT_PRINTF(fmtpos, argpos)
#endif

#ifdef WIN32
#define NBDKIT_DLLEXPORT __declspec(dllexport)
#else
#define NBDKIT_DLLEXPORT
#endif

#define NBDKIT_THREAD_MODEL_SERIALIZE_CONNECTIONS     0
#define NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS    1
#define NBDKIT_THREAD_MODEL_SERIALIZE_REQUESTS        2
#define NBDKIT_THREAD_MODEL_PARALLEL                  3

#define NBDKIT_FLAG_MAY_TRIM (1<<0) /* Maps to !NBD_CMD_FLAG_NO_HOLE */
#define NBDKIT_FLAG_FUA      (1<<1) /* Maps to NBD_CMD_FLAG_FUA */

#define NBDKIT_FUA_NONE       0
#define NBDKIT_FUA_EMULATE    1
#define NBDKIT_FUA_NATIVE     2

extern NBDKIT_DLLEXPORT void nbdkit_error (const char *msg, ...) ATTRIBUTE_FORMAT_PRINTF (1, 2);
extern NBDKIT_DLLEXPORT void nbdkit_verror (const char *msg, va_list args);
extern NBDKIT_DLLEXPORT void nbdkit_debug (const char *msg, ...) ATTRIBUTE_FORMAT_PRINTF (1, 2);
extern NBDKIT_DLLEXPORT void nbdkit_vdebug (const char *msg, va_list args);

extern NBDKIT_DLLEXPORT char *nbdkit_absolute_path (const char *path);
extern NBDKIT_DLLEXPORT int64_t nbdkit_parse_size (const char *str);
extern NBDKIT_DLLEXPORT int nbdkit_parse_bool (const char *str);
extern NBDKIT_DLLEXPORT int nbdkit_read_password (const char *value, char **password);
extern NBDKIT_DLLEXPORT char *nbdkit_realpath (const char *path);

/* A static non-NULL pointer which can be used when you don't need a
 * per-connection handle.
 */
#define NBDKIT_HANDLE_NOT_NEEDED (&errno)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#define NBDKIT_CXX_LANG_C extern "C"
#else
#define NBDKIT_CXX_LANG_C /* nothing */
#endif

#endif /* NBDKIT_COMMON_H */
