# cl-probe (probe.c), cl-trace (layer.c) and clbuild (clbuild.c), for the Android container:
# built with the NDK.
#   make NDK=<android-ndk> CL_HEADERS=<OpenCL-Headers>     (tools/android-ndk fetches the NDK)
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CL_HEADERS ?= $(HOME)/.cache/mobarchy/OpenCL-Headers
CC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS = -O2 -Wall -Wno-unused-parameter -fPIC -I$(CL_HEADERS) -I.
LDFLAGS = -shared -Wl,--version-script=exports.map -Wl,-z,defs
LDLIBS = -llog

all: libclprobe.so libcltrace.so clbuild

libclprobe.so: probe.c gen.c probe.h exports.map
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ probe.c gen.c $(LDLIBS)

libcltrace.so: layer.c gen-layer.c layer.h probe.h exports-layer.map
	$(CC) $(CFLAGS) -shared -Wl,--version-script=exports-layer.map -Wl,-z,defs -o $@ layer.c gen-layer.c $(LDLIBS)

clbuild: clbuild.c
	$(CC) -O2 -Wall -I$(CL_HEADERS) -o $@ $<

gen-layer.c: gen.py layer.c
	python3 gen.py --layer $(CL_HEADERS)/CL layer.c > $@.tmp && mv $@.tmp $@

gen.c: gen.py probe.c
	python3 gen.py $(CL_HEADERS)/CL probe.c > $@.tmp && mv $@.tmp $@

clean:
	rm -f libclprobe.so libcltrace.so clbuild gen.c gen-layer.c
