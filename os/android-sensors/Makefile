# mobarchy-sensormanager (sensormanager.cpp), for the Android container: built with the NDK.
#   make NDK=<android-ndk>        (see os/android-mesa/build for the NDK)
NDK ?= $(HOME)/.cache/mobarchy/android-ndk-r30
CXX = $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android30-clang++
CXXFLAGS = -O2 -Wall -Wextra

mobarchy-sensormanager: sensormanager.cpp
	$(CXX) $(CXXFLAGS) -o $@ sensormanager.cpp -llog -ldl -static-libstdc++

clean:
	rm -f mobarchy-sensormanager
