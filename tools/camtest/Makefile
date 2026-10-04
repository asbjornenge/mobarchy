# Camera test clients for the Android container (ADR 0017), built with the NDK.
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS = -O2 -Wall

all: camlist camshot

camlist: camlist.c
	$(CC) $(CFLAGS) -o $@ $< -lcamera2ndk

camshot: camshot.c
	$(CC) $(CFLAGS) -o $@ $< -lcamera2ndk -lmediandk -landroid -ldl

clean:
	rm -f camlist camshot
