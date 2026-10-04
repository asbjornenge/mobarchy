# mobarchy-sensormanager (sensormanager.cpp), for the Android container: built with the NDK.
#   make ndk NDK=<android-ndk>    (tools/android-ndk fetches it)
# Package: tools/repo-build os/android-sensors (runs "make ndk" here first).
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CXX = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++
CXXFLAGS = -O2 -Wall -Wextra

ndk: mobarchy-sensormanager

mobarchy-sensormanager: sensormanager.cpp
	$(CXX) $(CXXFLAGS) -o $@ sensormanager.cpp -llog -ldl -static-libstdc++

install:
	install -Dm755 mobarchy-sensormanager -t $(DESTDIR)/usr/lib/mobarchy/android/system/bin

clean:
	rm -f mobarchy-sensormanager

.PHONY: ndk install clean
