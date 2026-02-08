/* nbdkit
 * Copyright Red Hat
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>

#define CL_TARGET_OPENCL_VERSION 200 /* OpenCL >= 2.0 */
#include <CL/cl.h>
#include <CL/cl_ext.h>

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>

#include "ascii-ctype.h"
#include "cleanup.h"
#include "isaligned.h"
#include "minmax.h"
#include "rounding.h"
#include "vector.h"

#include "opencl-errors.h"

static int64_t size = -1;       /* Limit/size of the device. */
static unsigned device_index;   /* device=N */
static const char *device_name; /* device=NAME */

static cl_context context;      /* OpenCL command context. */
static cl_command_queue queue;  /* OpenCL command queue. */

/* List of buffers. */

/* We always allocate buffers of this size, and the size of the disk
 * is rounded up to a multiple of this.  XXX Should be chosen
 * dynamically.
 */
#define BUFFER_SIZE (64*1024)

struct buffer {
  cl_mem buffer;                /* If NULL, sparse. */
};

DEFINE_VECTOR_TYPE(buffers, struct buffer);
static buffers buffer_map;      /* Buffers covering the disk. */

/* List of OpenCL devices. */
struct cl_device {
  cl_platform_id platform_id;
  cl_device_id device_id;
  char *name;
  char *vendor;
  cl_bool available;
  cl_ulong global_mem_size;
  cl_ulong max_mem_alloc_size;
  cl_uint queue_on_device_max_size;
  cl_uint queue_on_device_preferred_size;
};

static void
free_cl_device (struct cl_device dev)
{
  free (dev.name);
  free (dev.vendor);
}

DEFINE_VECTOR_TYPE(devices, struct cl_device);

static devices all_devices;     /* List of all OpenCL devices. */
static struct cl_device device; /* Specific device picked. */

static void
free_all_devices (void)
{
  devices_iter (&all_devices, free_cl_device);
  devices_reset (&all_devices);
}

/* Populate all_devices. */
static void
get_all_devices (void)
{
  const char *what;
  cl_int r;
  cl_uint num_platforms, num_devices;
  cl_platform_id *platform_ids = NULL;
  cl_device_id *device_ids = NULL;
  size_t pl_i, dev_i;
  size_t param_size;

  /* In case get_all_devices was called before, reset the list. */
  free_all_devices ();

  /* Build the list of all devices from all platforms as a flat list. */
  what = "clGetPlatformIDs";
  r = clGetPlatformIDs (0, NULL, &num_platforms);
  if (r == CL_PLATFORM_NOT_FOUND_KHR) {
    /* OpenCL seems to return this when no platform is detected at
     * all, so just return the empty list in this case.
     */
    return;
  }
  if (r != CL_SUCCESS) goto err;
  platform_ids = calloc (num_platforms, sizeof platform_ids[0]);
  if (!platform_ids) {
  mem_err:
    nbdkit_error ("allocating memory: %m");
    exit (EXIT_FAILURE);
  }
  r = clGetPlatformIDs (num_platforms, platform_ids, NULL);
  if (r != CL_SUCCESS) goto err;

  for (pl_i = 0; pl_i < num_platforms; ++pl_i) {
    what = "clGetDeviceIDs";
    r = clGetDeviceIDs (platform_ids[pl_i], CL_DEVICE_TYPE_ALL, 0, NULL,
                        &num_devices);
    if (r == CL_DEVICE_NOT_FOUND) {
      /* OpenCL seems to return this when the platform is found but
       * the vendor's library is not configured to access devices
       * within the platform; skip over this platform.
       */
      continue;
    }
    if (r != CL_SUCCESS) goto err;
    free (device_ids);
    device_ids = calloc (num_devices, sizeof device_ids[0]);
    if (!device_ids) goto mem_err;
    r = clGetDeviceIDs (platform_ids[pl_i], CL_DEVICE_TYPE_ALL,
                        num_devices, device_ids, NULL);
    if (r != CL_SUCCESS) goto err;

    if (devices_reserve (&all_devices, num_devices) == -1) goto mem_err;

    for (dev_i = 0; dev_i < num_devices; ++dev_i) {
      struct cl_device dev = {
        .platform_id = platform_ids[pl_i],
        .device_id = device_ids[dev_i],
      };

      what = "clGetDeviceInfo: CL_DEVICE_NAME";
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_NAME,
                           0, NULL, &param_size);
      if (r != CL_SUCCESS) goto err;
      dev.name = calloc (param_size, sizeof (char));
      if (!dev.name) goto mem_err;
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_NAME,
                           param_size, dev.name, NULL);
      if (r != CL_SUCCESS) goto err;

      what = "clGetDeviceInfo: CL_DEVICE_VENDOR";
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_VENDOR,
                           0, NULL, &param_size);
      if (r != CL_SUCCESS) goto err;
      dev.vendor = calloc (param_size, sizeof (char));
      if (!dev.vendor) goto mem_err;
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_VENDOR,
                           param_size, dev.vendor, NULL);
      if (r != CL_SUCCESS) goto err;

      /* XXX clinfo manages to get the board name.  How? */

      what = "clGetDeviceInfo: CL_DEVICE_AVAILABLE";
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_AVAILABLE,
                           sizeof (dev.available), &dev.available, NULL);
      if (r != CL_SUCCESS) goto err;

      what = "clGetDeviceInfo: CL_DEVICE_GLOBAL_MEM_SIZE";
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_GLOBAL_MEM_SIZE,
                           sizeof (dev.global_mem_size),
                           &dev.global_mem_size,
                           NULL);
      if (r != CL_SUCCESS) goto err;

      what = "clGetDeviceInfo: CL_DEVICE_MAX_MEM_ALLOC_SIZE";
      r = clGetDeviceInfo (device_ids[dev_i], CL_DEVICE_MAX_MEM_ALLOC_SIZE,
                           sizeof (dev.max_mem_alloc_size),
                           &dev.max_mem_alloc_size,
                           NULL);
      if (r != CL_SUCCESS) goto err;

      what = "clGetDeviceInfo: CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE";
      r = clGetDeviceInfo (device_ids[dev_i],
                           CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE,
                           sizeof (dev.queue_on_device_max_size),
                           &dev.queue_on_device_max_size,
                           NULL);
      if (r != CL_SUCCESS) goto err;

      what = "clGetDeviceInfo: CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE";
      r = clGetDeviceInfo (device_ids[dev_i],
                           CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE,
                           sizeof (dev.queue_on_device_preferred_size),
                           &dev.queue_on_device_preferred_size,
                           NULL);
      if (r != CL_SUCCESS) goto err;

      devices_append (&all_devices, dev);
    }
  }

  free (device_ids);
  free (platform_ids);
  return;

 err:
  opencl_to_error (r, what);
  exit (EXIT_FAILURE);
}

static void
vram_unload (void)
{
  free_all_devices ();

  if (queue)
    clReleaseCommandQueue (queue);
  if (context)
    clReleaseContext (context);
}

static int
vram_config (const char *key, const char *value)
{
  if (strcmp (key, "device") == 0) {
    if (!value[0]) {
      nbdkit_error ("missing device name or index");
      return -1;
    }
    if (ascii_isdigit (value[0])) {
      if (nbdkit_parse_unsigned ("device", value, &device_index) == -1)
        return -1;
    }
    else
      device_name = value;
  }
  else if (strcmp (key, "size") == 0) {
    size = nbdkit_parse_size (value);
    if (size == -1)
      return -1;
  }
  else {
    nbdkit_error ("unknown parameter '%s'", key);
    return -1;
  }

  return 0;
}

#define vram_config_help \
  "size=<SIZE>                    Limit the size of the disk.\n" \
  "device=<N|NAME>                Select which GPU to use."

static void
vram_dump_plugin (void)
{
  size_t i;

  get_all_devices ();

  printf ("vram_num_devices=%zu\n", all_devices.len);

  for (i = 0; i < all_devices.len; ++i) {
    printf ("vram_device_index=%zu\n", i);
    printf ("vram_device_name=%s\n", all_devices.ptr[i].name);
    printf ("vram_device_vendor=%s\n", all_devices.ptr[i].vendor);
    printf ("vram_device_available=%s\n",
            all_devices.ptr[i].available ? "yes" : "no");
    printf ("vram_device_mem_size=%" PRIu64 "\n",
            (uint64_t) all_devices.ptr[i].global_mem_size);
    printf ("vram_device_max_memory_allocation=%" PRIu64 "\n",
            (uint64_t) all_devices.ptr[i].max_mem_alloc_size);
    printf ("vram_device_queue_on_device_max_size=%u\n",
            all_devices.ptr[i].queue_on_device_max_size);
    printf ("vram_device_queue_on_device_preferred_size=%u\n",
            all_devices.ptr[i].queue_on_device_preferred_size);
  }

  free_all_devices ();
}

static int
vram_config_complete (void)
{
  size_t i;

  get_all_devices ();

  if (all_devices.len == 0) {
    nbdkit_error ("no OpenCL devices were detected. Try running 'clinfo' "
                  "to check that OpenCL is configured correctly.");
    return -1;
  }

  /* Pick the device. */
  if (device_name != NULL) {    /* device=NAME */
    for (i = 0; i < all_devices.len; ++i) {
      if (strcmp (all_devices.ptr[i].name, device_name) == 0) {
        device_index = i;
        break;
      }
    }
    if (i == all_devices.len) {
      nbdkit_error ("no OpenCL device called '%s' was found. Try "
                    "running 'nbdkit vram --dump-plugin'",
                    device_name);
      return -1;
    }
  }

  if (device_index >= all_devices.len) {
    nbdkit_error ("OpenCL device %u not found, there are only "
                  "%zu device(s) attached to the system. Try "
                  "running 'nbdkit vram --dump-plugin'",
                  device_index, all_devices.len);
    return -1;
  }

  device = all_devices.ptr[device_index];

  if (device.max_mem_alloc_size < BUFFER_SIZE) {
    nbdkit_error ("OpenCL device '%s' does not support buffers of size "
                  "at least %d. This is probably an internal error.",
                  device.name, BUFFER_SIZE);
    return -1;
  }

  nbdkit_debug ("picked OpenCL device %u: %s", device_index, device.name);

  /* Pick the size. */
  if (size >= 0) {              /* size appeared on the command line */
    if (size > device.global_mem_size) {
      nbdkit_error ("OpenCL device '%s' has size %" PRIu64" "
                    "which is smaller than "
                    "the size given on the command line. To allocate the "
                    "maximum size, omit the size parameter.",
                    device.name, (uint64_t) device.global_mem_size);
      return -1;
    }
  }
  else {                        /* else default to largest size */
    size = device.global_mem_size;
  }

  /* Round the size up to the nearest buffer size. */
  size = ROUND_UP (size, BUFFER_SIZE);
  nbdkit_debug ("size: %" PRIi64, size);

  return 0;
}

static pid_t pid_check;

static int
vram_get_ready (void)
{
  pid_check = getpid ();
  return 0;
}

static int
vram_after_fork (void)
{
  size_t i;
  cl_int r;

  /* In my testing, OpenCL (AMD's implementation anyway) would hang
   * after fork().  Detect this here and refuse to continue.
   */
  if (pid_check != getpid ()) {
    nbdkit_error ("detected fork! You must use 'nbdkit -f' with this plugin.");
    return -1;
  }

  assert (device.platform_id);
  assert (device.device_id);

  /* Allocate buffer map. */
  if (buffers_reserve (&buffer_map, size / BUFFER_SIZE) == -1) {
    nbdkit_error ("realloc: %m");
    return -1;
  }
  for (i = 0; i < size / BUFFER_SIZE; ++i) {
    struct buffer b = { .buffer = NULL };
    buffers_append (&buffer_map, b);
  }

  /* Create the command context & command queue. */
  const cl_context_properties context_properties[] = {
    CL_CONTEXT_PLATFORM, (long)device.platform_id,
    0
  };
  context = clCreateContextFromType (context_properties, CL_DEVICE_TYPE_DEFAULT,
                                     NULL, NULL, &r);
  if (r != CL_SUCCESS) {
    opencl_to_error (r, "clCreateContext");
    return -1;
  }
  nbdkit_debug ("clCreateContextFromType successful");

  const cl_queue_properties queue_properties[] = {
    CL_QUEUE_PROPERTIES, 0,
    0
  };
  queue = clCreateCommandQueueWithProperties (context, device.device_id,
                                              queue_properties,
                                              &r);
  if (r != CL_SUCCESS) {
    opencl_to_error (r, "clCreateCommandQueueWithProperties");
    return -1;
  }
  nbdkit_debug ("clCreateCommandQueueWithProperties successful");

  return 0;
}

static void *
vram_open (int readonly)
{
  return NBDKIT_HANDLE_NOT_NEEDED;
}

/* In theory we could be fully parallel, but we'd need to at least
 * lock buffers.
 */
#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

/* Get the size. */
static int64_t
vram_get_size (void *handle)
{
  return size;
}

/* The video RAM is not usually byte addressible, so hint to the
 * caller that larger blocks are preferred.
 */
static int
vram_block_size (void *handle,
                 uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
  /* On the GPUs that I have available, the minimum alignment is 256
   * bytes, so choose something a bit larger.  We should really
   * calculate this dynamically using device info. XXX
   */
  *minimum = 4096;
  *preferred = BUFFER_SIZE;
  *maximum = 0xffffffff;
  return 0;
}

/* Serves the same data over multiple connections. */
static int
vram_can_multi_conn (void *handle)
{
  return 1;
}

/* Read a whole buffer to 'buf'.  For simplicity (mainly to avoid
 * alignment hassle) this always operates on the entire BUFFER_SIZE,
 * even though the OpenCL APIs would allow reading partial buffers.
 */
static int
read_buffer (uint64_t bufnum, void *buf)
{
  struct buffer b = buffer_map.ptr[bufnum];
  cl_int r;

  if (b.buffer) {               /* Allocated buffer. */
    r = clEnqueueReadBuffer (queue, b.buffer, true, 0, BUFFER_SIZE, buf,
                             0, NULL, NULL);
    if (r != CL_SUCCESS) {
      opencl_to_error (r, "clEnqueueReadBuffer");
      return -1;
    }
  }
  else {                        /* Sparse. */
    memset (buf, 0, BUFFER_SIZE);
  }

  return 0;
}

/* Read video RAM. */
static int
vram_pread (void *handle, void *buf, uint32_t count, uint64_t offset,
            uint32_t flags)
{
  CLEANUP_FREE uint8_t *bounce = NULL;
  uint64_t bufnum, bufoffs;

  if (!IS_ALIGNED (count | offset, BUFFER_SIZE)) {
    bounce = malloc (BUFFER_SIZE);
    if (bounce == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  bufnum = offset / BUFFER_SIZE;  /* buffer number */
  bufoffs = offset % BUFFER_SIZE; /* offset within the buffer */

  /* Unaligned head */
  if (bufoffs) {
    uint64_t n = MIN (BUFFER_SIZE - bufoffs, count);

    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memcpy (buf, &bounce[bufoffs], n);

    buf += n;
    count -= n;
    offset += n;
    bufnum++;
  }

  /* Aligned body */
  while (count >= BUFFER_SIZE) {
    if (read_buffer (bufnum, buf) == -1)
      return -1;

    buf += BUFFER_SIZE;
    count -= BUFFER_SIZE;
    offset += BUFFER_SIZE;
    bufnum++;
  }

  /* Unaligned tail */
  if (count) {
    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memcpy (buf, bounce, count);
  }

  return 0;
}

static int
write_buffer (const void *buf, uint64_t bufnum)
{
  struct buffer b = buffer_map.ptr[bufnum];
  cl_int r;

  /* Allocate the buffer on the GPU, if not allocated yet. */
  if (!b.buffer) {
    b.buffer = clCreateBuffer (context, CL_MEM_READ_WRITE,
                               BUFFER_SIZE, NULL, &r);
    if (r != CL_SUCCESS) {
      opencl_to_error (r, "clCreateBuffer");
      return -1;
    }
    buffer_map.ptr[bufnum] = b;
  }

  /* XXX This is blocking (3rd parameter is 'true').  It could be made
   * non-blocking, but you *must* keep 'buf' around until the event
   * has completed, which basically means making a full copy of 'buf'
   * since nbdkit will reuse the memory as soon as we return.  Also we
   * would have to deal with synchronizing read-after-write.
   */
  r = clEnqueueWriteBuffer (queue, b.buffer, true, 0, BUFFER_SIZE, buf,
                            0, NULL, NULL);
  if (r != CL_SUCCESS) {
    opencl_to_error (r, "clEnqueueWriteBuffer");
    return -1;
  }

  return 0;
}

static int vram_flush (void *handle, uint32_t flags);

/* Write video RAM. */
static int
vram_pwrite (void *handle, const void *buf, uint32_t count, uint64_t offset,
             uint32_t flags)
{
  CLEANUP_FREE uint8_t *bounce = NULL;
  uint64_t bufnum, bufoffs;

  if (!IS_ALIGNED (count | offset, BUFFER_SIZE)) {
    bounce = malloc (BUFFER_SIZE);
    if (bounce == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  bufnum = offset / BUFFER_SIZE;  /* buffer number */
  bufoffs = offset % BUFFER_SIZE; /* offset within the buffer */

  /* Unaligned head */
  if (bufoffs) {
    uint64_t n = MIN (BUFFER_SIZE - bufoffs, count);

    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memcpy (&bounce[bufoffs], buf, n);
    if (write_buffer (bounce, bufnum) == -1)
      return -1;

    buf += n;
    count -= n;
    offset += n;
    bufnum++;
  }

  /* Aligned body */
  while (count >= BUFFER_SIZE) {
    if (write_buffer (buf, bufnum) == -1)
      return -1;

    buf += BUFFER_SIZE;
    count -= BUFFER_SIZE;
    offset += BUFFER_SIZE;
    bufnum++;
  }

  /* Unaligned tail */
  if (count) {
    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memcpy (bounce, buf, count);
    if (write_buffer (bounce, bufnum) == -1)
      return -1;
  }

  if (flags & NBDKIT_FLAG_FUA && vram_flush (handle, 0) == -1)
    return -1;

  return 0;
}

static int
vram_flush (void *handle, uint32_t flags)
{
  /* XXX We _could_ flush by waiting until all write events have
   * completed, but if you power down the system then you are still
   * going to lose all your data, at least on any ordinary consumer
   * GPU.
   */
  return 0;
}

/* Note must call clFinish(queue) after this. */
static int
zero_buffer_async (uint64_t bufnum)
{
  struct buffer b = buffer_map.ptr[bufnum];
  const uint8_t z = 0;
  cl_int r;

  /* Allocate the buffer, if not allocated yet. */
  if (!b.buffer) {
    b.buffer = clCreateBuffer (context, CL_MEM_READ_WRITE,
                               BUFFER_SIZE, NULL, &r);
    if (r != CL_SUCCESS) {
      opencl_to_error (r, "clCreateBuffer");
      return -1;
    }
    buffer_map.ptr[bufnum] = b;
  }

  r = clEnqueueFillBuffer (queue, b.buffer, &z, sizeof (uint8_t),
                           0, BUFFER_SIZE,
                           0, NULL, NULL);
  if (r != CL_SUCCESS) {
    opencl_to_error (r, "clEnqueueFillBuffer");
    return -1;
  }

  return 0;
}

/* Because video RAM is contended with other users, the zero operation
 * here keeps RAM allocated.  If you want to give up video RAM to
 * other users, then use trim instead.
 */
static int
vram_zero (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  CLEANUP_FREE uint8_t *bounce = NULL;
  uint64_t bufnum, bufoffs;
  cl_int r;

  if (!IS_ALIGNED (count | offset, BUFFER_SIZE)) {
    bounce = malloc (BUFFER_SIZE);
    if (bounce == NULL) {
      nbdkit_error ("malloc: %m");
      return -1;
    }
  }

  bufnum = offset / BUFFER_SIZE;  /* buffer number */
  bufoffs = offset % BUFFER_SIZE; /* offset within the buffer */

  /* Unaligned head */
  if (bufoffs) {
    uint64_t n = MIN (BUFFER_SIZE - bufoffs, count);

    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memset (&bounce[bufoffs], 0, n);
    if (write_buffer (bounce, bufnum) == -1)
      return -1;

    count -= n;
    offset += n;
    bufnum++;
  }

  /* Aligned body */
  while (count >= BUFFER_SIZE) {
    if (zero_buffer_async (bufnum) == -1)
      return -1;

    count -= BUFFER_SIZE;
    offset += BUFFER_SIZE;
    bufnum++;
  }

  /* Write pending zero commands. */
  r = clFinish (queue);
  if (r != CL_SUCCESS) {
    opencl_to_error (r, "zero: clFinish");
    return -1;
  }

  /* Unaligned tail */
  if (count) {
    if (read_buffer (bufnum, bounce) == -1)
      return -1;
    memset (bounce, 0, count);
    if (write_buffer (bounce, bufnum) == -1)
      return -1;
  }

  if (flags & NBDKIT_FLAG_FUA && vram_flush (handle, 0) == -1)
    return -1;

  return 0;
}

static int
free_buffer (uint64_t bufnum)
{
  struct buffer b = buffer_map.ptr[bufnum];
  cl_int r;

  if (b.buffer) {
    r = clReleaseMemObject (b.buffer);
    if (r != CL_SUCCESS) {
      opencl_to_error (r, "clReleaseMemObject");
      return -1;
    }

    b.buffer = NULL;
    buffer_map.ptr[bufnum] = b;
  }

  return 0;
}

/* Trim video RAM and give it back to the system if possible. */
static int
vram_trim (void *handle, uint32_t count, uint64_t offset, uint32_t flags)
{
  uint64_t bufnum, bufoffs;

  bufnum = offset / BUFFER_SIZE;  /* buffer number */
  bufoffs = offset % BUFFER_SIZE; /* offset within the buffer */

  if (bufoffs) {
    uint64_t n = MIN (BUFFER_SIZE - bufoffs, count);

    count -= n;
    offset += n;
    bufnum++;
  }

  /* Aligned body */
  while (count >= BUFFER_SIZE) {
    free_buffer (bufnum);

    count -= BUFFER_SIZE;
    offset += BUFFER_SIZE;
    bufnum++;
  }

  if (flags & NBDKIT_FLAG_FUA && vram_flush (handle, 0) == -1)
    return -1;

  return 0;
}

static uint32_t
get_buffer_type (uint64_t bufnum)
{
  const struct buffer b = buffer_map.ptr[bufnum];

  /* XXX Could check if the buffer is all zeroes and return
   * NBDKIT_EXTENT_ZERO.
   */
  if (b.buffer != NULL)
    return 0;                   /* Allocated data. */
  else
    return NBDKIT_EXTENT_HOLE | NBDKIT_EXTENT_ZERO;
}

static int
vram_extents (void *handle, uint32_t count, uint64_t offset,
              uint32_t flags, struct nbdkit_extents *extents)
{
  uint64_t bufnum, bufoffs;

  bufnum = offset / BUFFER_SIZE;  /* buffer number */
  bufoffs = offset % BUFFER_SIZE; /* offset within the buffer */

  /* Unaligned head */
  if (bufoffs) {
    uint64_t n = MIN (BUFFER_SIZE - bufoffs, count);

    if (nbdkit_add_extent (extents, offset, n,
                           get_buffer_type (bufnum)) == -1)
      return -1;

    count -= n;
    offset += n;
    bufnum++;
  }

  /* Aligned body */
  while (count >= BUFFER_SIZE) {
    if (nbdkit_add_extent (extents, offset, BUFFER_SIZE,
                           get_buffer_type (bufnum)) == -1)
      return -1;

    count -= BUFFER_SIZE;
    offset += BUFFER_SIZE;
    bufnum++;
  }

  /* Unaligned tail */
  if (count) {
    if (nbdkit_add_extent (extents, offset, count,
                           get_buffer_type (bufnum)) == -1)
      return -1;
  }

  return 0;
}

static struct nbdkit_plugin plugin = {
  .name              = "vram",
  .longname          = "nbdkit vram plugin",
  .version           = PACKAGE_VERSION,
  .unload            = vram_unload,
  .dump_plugin       = vram_dump_plugin,
  .config            = vram_config,
  .config_help       = vram_config_help,
  .config_complete   = vram_config_complete,
  .magic_config_key  = "size",
  .get_ready         = vram_get_ready,
  .after_fork        = vram_after_fork,
  .open              = vram_open,
  .get_size          = vram_get_size,
  .block_size        = vram_block_size,
  .can_multi_conn    = vram_can_multi_conn,
  .pread             = vram_pread,
  .pwrite            = vram_pwrite,
  .flush             = vram_flush,
  .zero              = vram_zero,
  .trim              = vram_trim,
  .extents           = vram_extents,
  .errno_is_preserved = 1,
};

NBDKIT_REGISTER_PLUGIN (plugin)
