# mobarchy-android-services: see mobarchy-android-services.c. Build: make; install: make install DESTDIR=...
PKGS = libgbinder libglibutil glib-2.0
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

mobarchy-android-services: mobarchy-android-services.c

install: mobarchy-android-services
	install -Dm755 mobarchy-android-services -t $(DESTDIR)/usr/lib/mobarchy
	install -Dm644 mobarchy-android-services.service -t $(DESTDIR)/usr/lib/systemd/system
	install -Dm644 mobarchy-android-services.sysusers $(DESTDIR)/usr/lib/sysusers.d/mobarchy-android-services.conf

clean:
	rm -f mobarchy-android-services

.PHONY: install clean
