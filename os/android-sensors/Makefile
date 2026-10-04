# The Android container's sensor programs, built with the NDK: mobarchy-sensormanager
# (sensormanager.cpp: ISensorManager for HALs) and mobarchy-sensord (sensord.c: the sensor
# bridge, ADR 0020).
#   make ndk NDK=<android-ndk>    (tools/android-ndk fetches it)
# And proxy/: mobarchy-sensor-proxy, their Linux side (make proxy).
# Package: tools/repo-build os/android-sensors (runs "make ndk" here first).
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CXX = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++
CXXFLAGS = -O2 -Wall -Wextra
CC = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter

ndk: mobarchy-sensormanager mobarchy-sensord

mobarchy-sensord: sensord.c protocol.h
	$(CC) $(CFLAGS) -o $@ sensord.c -landroid -llog

mobarchy-sensormanager: sensormanager.cpp
	$(CXX) $(CXXFLAGS) -o $@ sensormanager.cpp -llog -ldl -static-libstdc++

proxy:
	$(MAKE) -C proxy

install:
	install -Dm755 mobarchy-sensormanager mobarchy-sensord -t $(DESTDIR)/usr/lib/mobarchy/android/system/bin
	$(MAKE) -C proxy install

clean:
	rm -f mobarchy-sensormanager mobarchy-sensord
	$(MAKE) -C proxy clean

.PHONY: ndk proxy install clean
