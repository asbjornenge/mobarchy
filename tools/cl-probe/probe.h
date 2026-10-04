/* cl-probe: see probe.c */
#pragma once

#define CL_TARGET_OPENCL_VERSION 310   /* every dispatch slot typed */
#define CL_USE_DEPRECATED_OPENCL_1_0_APIS
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#define CL_USE_DEPRECATED_OPENCL_2_1_APIS
#define CL_USE_DEPRECATED_OPENCL_2_2_APIS
#define CL_USE_DEPRECATED_OPENCL_3_0_APIS
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <CL/cl_gl.h>
#include <CL/cl_egl.h>
#include <CL/cl_icd.h>
#include <stddef.h>

struct probe_ext {
    const char *name;
    void *fn;
};

extern const cl_icd_dispatch probe_dispatch;
extern const struct probe_ext probe_extensions[];

void probe_unimplemented(const char *name);
