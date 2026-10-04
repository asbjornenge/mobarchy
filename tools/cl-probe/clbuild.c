/*
 * clbuild: builds an OpenCL C file on the first OpenCL device and prints the build log, for
 * reproducing a HAL's program builds (cl-trace saves their sources) in the container.
 *
 *     clbuild <file.cl> [options]
 */
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: clbuild <file.cl> [options]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *src = malloc(n + 1);
    if (fread(src, 1, n, f) != (size_t)n)
        return 1;
    src[n] = 0;
    fclose(f);

    /* through Android's ICD loader, as the HALs */
    void *cl = dlopen("libOpenCL.so", RTLD_NOW);
    if (!cl) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
#define F(name) __typeof__(&name) p_##name = dlsym(cl, #name)
    F(clGetPlatformIDs); F(clGetDeviceIDs); F(clCreateContext); F(clCreateProgramWithSource);
    F(clBuildProgram); F(clGetProgramBuildInfo);
#define clGetPlatformIDs p_clGetPlatformIDs
#define clGetDeviceIDs p_clGetDeviceIDs
#define clCreateContext p_clCreateContext
#define clCreateProgramWithSource p_clCreateProgramWithSource
#define clBuildProgram p_clBuildProgram
#define clGetProgramBuildInfo p_clGetProgramBuildInfo
    cl_platform_id p;
    cl_device_id d;
    cl_int err;
    if (clGetPlatformIDs(1, &p, NULL) || clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 1, &d, NULL)) {
        fprintf(stderr, "no OpenCL device\n");
        return 1;
    }
    cl_context c = clCreateContext(NULL, 1, &d, NULL, NULL, &err);
    const char *s = src;
    cl_program prog = clCreateProgramWithSource(c, 1, &s, NULL, &err);
    err = clBuildProgram(prog, 1, &d, argc > 2 ? argv[2] : "", NULL, NULL);
    size_t size;
    clGetProgramBuildInfo(prog, d, CL_PROGRAM_BUILD_LOG, 0, NULL, &size);
    char *log = malloc(size + 1);
    clGetProgramBuildInfo(prog, d, CL_PROGRAM_BUILD_LOG, size, log, NULL);
    printf("build: %d\n%s\n", err, log);
    return err != CL_SUCCESS;
}
