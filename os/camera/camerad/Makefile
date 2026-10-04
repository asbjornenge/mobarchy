# mobarchy-camerad (camerad.c), for the Android container: built with the NDK.
#   make NDK=<android-ndk>        (tools/android-ndk fetches it)
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter

mobarchy-camerad: camerad.c ../protocol.h
	$(CC) $(CFLAGS) -o $@ camerad.c -lcamera2ndk -lmediandk -landroid -lbinder_ndk -llog -ldl

clean:
	rm -f mobarchy-camerad
