# The camera bridge (ADR 0018), both ends:
#   camerad/   mobarchy-camerad, in the Android container: built on the host with the
#              Android NDK (tools/android-ndk): make ndk
#   source/    mobarchy-camera-source, on the Linux side: make
# Package: tools/repo-build os/camera (runs "make ndk" here first, then the PKGBUILD).
all: source

ndk:
	$(MAKE) -C camerad

source:
	$(MAKE) -C source

install:
	install -Dm755 camerad/mobarchy-camerad -t $(DESTDIR)/usr/lib/mobarchy/android/system/bin
	$(MAKE) -C source install

clean:
	$(MAKE) -C camerad clean
	$(MAKE) -C source clean

.PHONY: all ndk source install clean
