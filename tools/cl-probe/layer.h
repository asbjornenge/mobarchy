/* cl-trace: see layer.c */
#pragma once

#include "probe.h"
#include <CL/cl_layer.h>

#include <android/log.h>
#include <stdatomic.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "cltrace", __VA_ARGS__)

extern const cl_icd_dispatch *next;
void trace_fill(cl_icd_dispatch *d);
