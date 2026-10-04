/*
 * cl-trace: an OpenCL layer (Khronos ICD loader) for the Android container (ADR 0017). It
 * passes every call on to the driver and logs (logcat tag "cltrace") each function's first
 * call and every failure; programs that fail to build are logged with their build log,
 * and every program's source is saved to DUMP_DIR. For finding what a vendor HAL needs
 * from Mesa's OpenCL (rusticl).
 *
 * Enable for a service with: setenv OPENCL_LAYERS /odm/lib64/libcltrace.so
 */

#include "layer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DUMP_DIR "/data/vendor/camera/cltrace"

const cl_icd_dispatch *next;
static cl_icd_dispatch dispatch;

CL_API_ENTRY cl_int CL_API_CALL
clGetLayerInfo(cl_layer_info param, size_t size, void *value, size_t *size_ret)
{
    switch (param) {
    case CL_LAYER_API_VERSION: {
        cl_layer_api_version v = CL_LAYER_API_VERSION_100;
        if (size_ret)
            *size_ret = sizeof v;
        if (value) {
            if (size < sizeof v)
                return CL_INVALID_VALUE;
            memcpy(value, &v, sizeof v);
        }
        return CL_SUCCESS;
    }
    case CL_LAYER_NAME: {
        static const char name[] = "cl-trace";
        if (size_ret)
            *size_ret = sizeof name;
        if (value) {
            if (size < sizeof name)
                return CL_INVALID_VALUE;
            memcpy(value, name, sizeof name);
        }
        return CL_SUCCESS;
    }
    }
    return CL_INVALID_VALUE;
}

CL_API_ENTRY cl_int CL_API_CALL
clInitLayer(cl_uint num_entries, const cl_icd_dispatch *target, cl_uint *num_entries_ret,
            const cl_icd_dispatch **layer_dispatch_ret)
{
    if (!target || !num_entries_ret || !layer_dispatch_ret)
        return CL_INVALID_VALUE;
    next = target;
    trace_fill(&dispatch);
    *num_entries_ret = sizeof dispatch / sizeof(void *);
    *layer_dispatch_ret = &dispatch;
    LOG("layer initialised");
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clInitLayerWithProperties(cl_uint num_entries, const cl_icd_dispatch *target,
                          cl_uint *num_entries_ret, const cl_icd_dispatch **layer_dispatch_ret,
                          const cl_layer_properties *properties)
{
    return clInitLayer(num_entries, target, num_entries_ret, layer_dispatch_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clDeinitLayer(void)
{
    return CL_SUCCESS;
}

/* --- programs ----------------------------------------------------------------------------- */

static atomic_int next_program = 1;

static void
dump(int id, const char *what, const void *data, size_t size)
{
    char path[128];
    mkdir(DUMP_DIR, 0770);
    snprintf(path, sizeof path, DUMP_DIR "/%d-%d-%s", getpid(), id, what);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
    if (fd < 0)
        return;
    if (write(fd, data, size) < 0)
        LOG("can't write %s: %s", path, strerror(errno));
    close(fd);
}

/* logcat lines are limited: long text goes out line by line */
static void
log_text(const char *prefix, const char *text)
{
    const char *line = text;
    while (*line) {
        const char *end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        LOG("%s%.*s", prefix, len > 900 ? 900 : len, line);
        line += len + (end ? 1 : 0);
    }
}

void
after_clCreateProgramWithSource(cl_program r, cl_context context, cl_uint count,
                                const char **strings, const size_t *lengths, cl_int *errcode_ret)
{
    if (!r)
        return;
    int id = atomic_fetch_add(&next_program, 1);
    size_t total = 0;
    for (cl_uint i = 0; i < count; i++)
        total += lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
    char *src = malloc(total + 1), *p = src;
    for (cl_uint i = 0; i < count; i++) {
        size_t l = lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
        memcpy(p, strings[i], l);
        p += l;
    }
    dump(id, "source.cl", src, total);
    LOG("program %p: source %zu bytes, saved as %d-%d-source.cl", (void *)r, total, getpid(), id);
    free(src);
}

void
after_clCreateProgramWithBinary(cl_program r, cl_context context, cl_uint n,
                                const cl_device_id *devices, const size_t *lengths,
                                const unsigned char **binaries, cl_int *binary_status,
                                cl_int *errcode_ret)
{
    LOG("program %p: from a binary of %zu bytes (%s)", (void *)r, lengths[0],
        r ? "accepted" : "refused");
}

static void
build_log(cl_program program, const char *what, cl_int r, const char *options)
{
    cl_device_id dev;
    size_t size = 0;
    if (r == CL_SUCCESS)
        return;
    LOG("%s of program %p failed (%d), options \"%s\"", what, (void *)program, r,
        options ? options : "");
    if (next->clGetProgramInfo(program, CL_PROGRAM_DEVICES, sizeof dev, &dev, NULL) != CL_SUCCESS ||
        next->clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &size) != CL_SUCCESS)
        return;
    char *log = calloc(1, size + 1);
    next->clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, size, log, NULL);
    log_text("  | ", log);
    free(log);
}

void
after_clBuildProgram(cl_int r, cl_program program, cl_uint n, const cl_device_id *devices,
                     const char *options, void (CL_CALLBACK *notify)(cl_program, void *),
                     void *user_data)
{
    build_log(program, "build", r, options);
}

void
after_clCompileProgram(cl_int r, cl_program program, cl_uint n, const cl_device_id *devices,
                       const char *options, cl_uint nheaders, const cl_program *headers,
                       const char **names, void (CL_CALLBACK *notify)(cl_program, void *),
                       void *user_data)
{
    build_log(program, "compile", r, options);
}

void
after_clCreateKernel(cl_kernel r, cl_program program, const char *name, cl_int *errcode_ret)
{
    if (!r)
        LOG("  (kernel \"%s\" of program %p)", name, (void *)program);
}

void
after_clGetDeviceInfo(cl_int r, cl_device_id device, cl_device_info param, size_t size,
                      void *value, size_t *size_ret)
{
    if (r != CL_SUCCESS)
        LOG("  (clGetDeviceInfo 0x%x)", param);
}

void
after_clGetExtensionFunctionAddressForPlatform(void *r, cl_platform_id platform, const char *name)
{
    if (!r)
        LOG("extension function %s: not there", name);
}
