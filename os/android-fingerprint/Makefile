# The Android container's fingerprint programs, built with the NDK (ADR 0021):
# mobarchy-sharedsecret (sharedsecret.c: KeyMint's HMAC key, which the fingerprint matcher
# needs) and mobarchy-fingerprintd (fingerprintd.c: the fingerprint bridge). And fprint/:
# mobarchy-fingerprint, their Linux side (make -C fprint).
# Package: tools/repo-build os/android-fingerprint (runs "make ndk" here first).
#   make ndk NDK=<android-ndk>    (tools/android-ndk fetches it)
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter

ndk: mobarchy-sharedsecret mobarchy-fingerprintd

mobarchy-sharedsecret: sharedsecret.c
	$(CC) $(CFLAGS) -o $@ sharedsecret.c -lbinder_ndk -llog -ldl

mobarchy-fingerprintd: fingerprintd.c protocol.h
	$(CC) $(CFLAGS) -o $@ fingerprintd.c -lbinder_ndk -llog -ldl

install:
	install -Dm755 mobarchy-sharedsecret mobarchy-fingerprintd -t $(DESTDIR)/usr/lib/mobarchy/android/system/bin
	$(MAKE) -C fprint install

clean:
	rm -f mobarchy-sharedsecret mobarchy-fingerprintd
	$(MAKE) -C fprint clean

.PHONY: ndk install clean
