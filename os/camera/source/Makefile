# mobarchy-camera-source (ADR 0018). Build: make; install: make install DESTDIR=...
PKGS = libpipewire-0.3 gio-2.0 gio-unix-2.0
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

mobarchy-camera-source: mobarchy-camera-source.c ../android-camera/protocol.h
	$(CC) $(CFLAGS) -o $@ mobarchy-camera-source.c $(LDLIBS)

install: mobarchy-camera-source
	install -Dm755 mobarchy-camera-source -t $(DESTDIR)/usr/lib/mobarchy
	install -Dm644 mobarchy-camera-source.service -t $(DESTDIR)/usr/lib/systemd/user

clean:
	rm -f mobarchy-camera-source

.PHONY: install clean
