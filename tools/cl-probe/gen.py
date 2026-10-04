#!/usr/bin/env python3
"""Generates cl-probe's dispatch table (gen.c) from the Khronos OpenCL headers.

Every function in the ICD dispatch table, and the extension functions in EXTENSIONS, gets
an entry: probe.c's implementation (p_<name>) if it has one, otherwise a generated stub
that logs the call and fails it (CL_INVALID_OPERATION), so we see what is asked for.

    gen.py <OpenCL-Headers/CL> <probe.c> > gen.c
    gen.py --layer <OpenCL-Headers/CL> <layer.c> > gen-layer.c

With --layer, it generates cl-trace instead (layer.c): an OpenCL layer whose functions call
the next layer or driver, log each function's first call and every failure, and call
layer.c's after_<name>(result, arguments...) where it has one.
"""
import re
import sys
from pathlib import Path

# extension functions handed out by clGetExtensionFunctionAddress (if probe.c advertises
# the extension); the camera HAL looks these up
EXTENSIONS = [
    "clImportMemoryARM",
    "clCreateCommandBufferKHR", "clFinalizeCommandBufferKHR", "clRetainCommandBufferKHR",
    "clReleaseCommandBufferKHR", "clEnqueueCommandBufferKHR", "clCommandNDRangeKernelKHR",
    "clCommandBarrierWithWaitListKHR", "clCommandCopyBufferKHR", "clCommandCopyBufferRectKHR",
    "clCommandCopyBufferToImageKHR", "clCommandCopyImageKHR", "clCommandCopyImageToBufferKHR",
    "clCommandFillBufferKHR", "clCommandFillImageKHR", "clGetCommandBufferInfoKHR",
    "clCreateSemaphoreWithPropertiesKHR", "clEnqueueWaitSemaphoresKHR",
    "clEnqueueSignalSemaphoresKHR", "clGetSemaphoreInfoKHR", "clReleaseSemaphoreKHR",
    "clRetainSemaphoreKHR", "clGetSemaphoreHandleForTypeKHR",
    "clCreateCommandQueueWithPropertiesKHR", "clGetKernelSubGroupInfoKHR",
    "clCreateProgramWithILKHR", "clTerminateContextKHR",
]


def prototypes(cl):
    text = "\n".join((cl / h).read_text() for h in
                     ["cl_function_types.h", "cl_ext.h", "cl_gl.h", "cl_egl.h"])
    out = {}
    pat = re.compile(r"typedef\s+([\w\s\*]+?)\s+CL_API_CALL\s+(\w+)_t\s*\(([^;]*?)\)\s*[\w\s]*;", re.S)
    for ret, name, params in pat.findall(text):
        params = " ".join(params.split())
        out.setdefault(name, (" ".join(ret.split()), params))
    return out


def dispatch_names(cl):
    text = (cl / "cl_icd.h").read_text()
    body = text[text.index("typedef struct _cl_icd_dispatch {"):text.index("} cl_icd_dispatch;")]
    # a member may be split over two lines: join them
    body = re.sub(r"(\w+_t \*)\s*\n\s*(\w+;)", r"\1\2", body)
    names, skip = [], False
    for line in body.splitlines():
        s = line.strip()
        if s.startswith("#ifdef _WIN32"):
            skip = True
        elif s.startswith("#else") or s.startswith("#endif"):
            skip = False
        m = re.match(r"(\w+)_t \*(\w+);", s)
        if m:
            names.append((m.group(2), skip))
    return names


def param_names(params):
    if params in ("", "void"):
        return []
    parts, depth, cur = [], 0, ""
    for c in params:
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
            continue
        depth += (c == "(") - (c == ")")
        cur += c
    parts.append(cur)
    names = []
    for p in parts:
        p = p.strip()
        m = re.search(r"\(\s*(?:CL_CALLBACK\s*)?\*\s*(\w+)\s*\)", p)   # a function pointer
        if not m:
            m = re.search(r"(\w+)\s*(\[[^\]]*\])?$", p)
        names.append(m.group(1))
    return names


def stub(name, ret, params):
    names = param_names(params)
    body = [f'    probe_unimplemented("{name}");']
    if "errcode_ret" in names:
        body.append("    if (errcode_ret) *errcode_ret = CL_INVALID_OPERATION;")
    for n in names:
        body.append(f"    (void){n};")
    if ret == "void":
        pass
    elif ret == "cl_int":
        body.append("    return CL_INVALID_OPERATION;")
    elif "*" in ret or ret.startswith("cl_") and ret not in ("cl_uint", "cl_ulong", "cl_bool"):
        body.append("    return NULL;")
    else:
        body.append("    return 0;")
    return f"static {ret} CL_API_CALL g_{name}({params or 'void'})\n{{\n" + "\n".join(body) + "\n}\n"


def layer_wrapper(name, ret, params, hooked):
    names = param_names(params)
    args = ", ".join(names)
    body = [f"    static atomic_flag seen = ATOMIC_FLAG_INIT;",
            f'    if (!atomic_flag_test_and_set(&seen)) LOG("first call: {name}");']
    if "errcode_ret" in names:
        body.append("    cl_int err_local = CL_SUCCESS;")
        body.append("    if (!errcode_ret) errcode_ret = &err_local;")
    call = f"next->{name}({args})"
    if ret == "void":
        body.append(f"    {call};")
    else:
        body.append(f"    {ret} r = {call};")
    if ret == "cl_int":
        body.append(f'    if (r != CL_SUCCESS) LOG("{name} failed: %d", r);')
    if "errcode_ret" in names:
        body.append(f'    if (*errcode_ret != CL_SUCCESS) LOG("{name} failed: %d", *errcode_ret);')
    if hooked:
        body.append(f"    after_{name}({'r, ' if ret != 'void' else ''}{args});")
    if ret != "void":
        body.append("    return r;")
    return f"static {ret} CL_API_CALL t_{name}({params or 'void'})\n{{\n" + "\n".join(body) + "\n}\n"


def main_layer(cl, layer):
    protos = prototypes(cl)
    hooked = set(re.findall(r"\bafter_(\w+)\s*\(", layer))
    print("/* generated by gen.py --layer: do not edit */")
    print('#include "layer.h"\n')
    names = [n for n, win in dispatch_names(cl) if not win]
    for name in names:
        if name not in protos:
            sys.exit(f"no prototype for {name}")
        ret, params = protos[name]
        if name in hooked:
            print(f"void after_{name}({ret + ', ' if ret != 'void' else ''}{params or 'void'});")
        print(layer_wrapper(name, ret, params, name in hooked))
    print("void trace_fill(cl_icd_dispatch *d)\n{")
    for name in names:
        print(f"    d->{name} = t_{name};")
    print("}")


def main():
    if sys.argv[1] == "--layer":
        main_layer(Path(sys.argv[2]), Path(sys.argv[3]).read_text())
        return
    cl, probe = Path(sys.argv[1]), Path(sys.argv[2]).read_text()
    protos = prototypes(cl)
    implemented = set(re.findall(r"CL_API_CALL\s+p_(\w+)\s*\(", probe))
    print("/* generated by gen.py: do not edit */")
    print('#include "probe.h"\n')
    wanted = [n for n, win in dispatch_names(cl)] + EXTENSIONS
    for name in dict.fromkeys(wanted):
        if name not in protos:
            continue
        ret, params = protos[name]
        if name in implemented:
            print(f"{ret} CL_API_CALL p_{name}({params or 'void'});")
        else:
            print(stub(name, ret, params))
    print("const cl_icd_dispatch probe_dispatch = {")
    for name, win in dispatch_names(cl):
        if win:
            continue        # not in the struct outside Windows
        if name in implemented:
            print(f"    .{name} = p_{name},")
        elif name in protos:
            print(f"    .{name} = g_{name},")
        else:
            sys.exit(f"no prototype for {name}: its dispatch slot would be NULL")
    print("};\n")
    print("const struct probe_ext probe_extensions[] = {")
    for name in EXTENSIONS:
        fn = f"p_{name}" if name in implemented else f"g_{name}"
        print(f'    {{"{name}", (void *){fn}}},')
    print("    {NULL, NULL},\n};")


main()
