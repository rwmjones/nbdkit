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

#ifndef NBDKIT_OPENCL_ERRORS_H
#define NBDKIT_OPENCL_ERRORS_H

/* Annoyingly there seems to be no way to automatically convert the
 * error code to a string.  This way is suggested by
 * https://stackoverflow.com/questions/24326432/convenient-way-to-show-opencl-error-codes
 */
#define case_return_string(x) case x: return #x

static const char *
opencl_errstr (cl_int err)
{
  switch (err) {
    case_return_string(CL_SUCCESS                        );
    case_return_string(CL_DEVICE_NOT_FOUND               );
    case_return_string(CL_DEVICE_NOT_AVAILABLE           );
    case_return_string(CL_COMPILER_NOT_AVAILABLE         );
    case_return_string(CL_MEM_OBJECT_ALLOCATION_FAILURE  );
    case_return_string(CL_OUT_OF_RESOURCES               );
    case_return_string(CL_OUT_OF_HOST_MEMORY             );
    case_return_string(CL_PROFILING_INFO_NOT_AVAILABLE   );
    case_return_string(CL_MEM_COPY_OVERLAP               );
    case_return_string(CL_IMAGE_FORMAT_MISMATCH          );
    case_return_string(CL_IMAGE_FORMAT_NOT_SUPPORTED     );
    case_return_string(CL_BUILD_PROGRAM_FAILURE          );
    case_return_string(CL_MAP_FAILURE                    );
    case_return_string(CL_MISALIGNED_SUB_BUFFER_OFFSET   );
    case_return_string(CL_COMPILE_PROGRAM_FAILURE        );
    case_return_string(CL_LINKER_NOT_AVAILABLE           );
    case_return_string(CL_LINK_PROGRAM_FAILURE           );
    case_return_string(CL_DEVICE_PARTITION_FAILED        );
    case_return_string(CL_KERNEL_ARG_INFO_NOT_AVAILABLE  );
    case_return_string(CL_INVALID_VALUE                  );
    case_return_string(CL_INVALID_DEVICE_TYPE            );
    case_return_string(CL_INVALID_PLATFORM               );
    case_return_string(CL_INVALID_DEVICE                 );
    case_return_string(CL_INVALID_CONTEXT                );
    case_return_string(CL_INVALID_QUEUE_PROPERTIES       );
    case_return_string(CL_INVALID_COMMAND_QUEUE          );
    case_return_string(CL_INVALID_HOST_PTR               );
    case_return_string(CL_INVALID_MEM_OBJECT             );
    case_return_string(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);
    case_return_string(CL_INVALID_IMAGE_SIZE             );
    case_return_string(CL_INVALID_SAMPLER                );
    case_return_string(CL_INVALID_BINARY                 );
    case_return_string(CL_INVALID_BUILD_OPTIONS          );
    case_return_string(CL_INVALID_PROGRAM                );
    case_return_string(CL_INVALID_PROGRAM_EXECUTABLE     );
    case_return_string(CL_INVALID_KERNEL_NAME            );
    case_return_string(CL_INVALID_KERNEL_DEFINITION      );
    case_return_string(CL_INVALID_KERNEL                 );
    case_return_string(CL_INVALID_ARG_INDEX              );
    case_return_string(CL_INVALID_ARG_VALUE              );
    case_return_string(CL_INVALID_ARG_SIZE               );
    case_return_string(CL_INVALID_KERNEL_ARGS            );
    case_return_string(CL_INVALID_WORK_DIMENSION         );
    case_return_string(CL_INVALID_WORK_GROUP_SIZE        );
    case_return_string(CL_INVALID_WORK_ITEM_SIZE         );
    case_return_string(CL_INVALID_GLOBAL_OFFSET          );
    case_return_string(CL_INVALID_EVENT_WAIT_LIST        );
    case_return_string(CL_INVALID_EVENT                  );
    case_return_string(CL_INVALID_OPERATION              );
    case_return_string(CL_INVALID_GL_OBJECT              );
    case_return_string(CL_INVALID_BUFFER_SIZE            );
    case_return_string(CL_INVALID_MIP_LEVEL              );
    case_return_string(CL_INVALID_GLOBAL_WORK_SIZE       );
    case_return_string(CL_INVALID_PROPERTY               );
    case_return_string(CL_INVALID_IMAGE_DESCRIPTOR       );
    case_return_string(CL_INVALID_COMPILER_OPTIONS       );
    case_return_string(CL_INVALID_LINKER_OPTIONS         );
    case_return_string(CL_INVALID_DEVICE_PARTITION_COUNT );

    /* Extended. */
    case_return_string(CL_PLATFORM_NOT_FOUND_KHR         );
  default: return "unknown OpenCL error";
  }
}

#undef case_return_string

#define opencl_to_error(err, what)                                      \
  nbdkit_error ("%s: error %d (%s)", what, err, opencl_errstr (err))

#endif /* NBDKIT_OPENCL_ERRORS_H */
