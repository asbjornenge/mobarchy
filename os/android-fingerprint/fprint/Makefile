# mobarchy-fingerprint (ADR 0021). Build: make; install: make install DESTDIR=...
PKGS = gio-2.0 gio-unix-2.0
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

mobarchy-fingerprint: mobarchy-fingerprint.c ../protocol.h
	$(CC) $(CFLAGS) -o $@ mobarchy-fingerprint.c $(LDLIBS)

install: mobarchy-fingerprint
	install -Dm755 mobarchy-fingerprint -t $(DESTDIR)/usr/lib/mobarchy
	install -Dm644 mobarchy-fingerprint.service -t $(DESTDIR)/usr/lib/systemd/system
	install -Dm644 net.reactivated.Fprint.conf -t $(DESTDIR)/usr/share/dbus-1/system.d
	install -Dm644 net.reactivated.fprint.policy -t $(DESTDIR)/usr/share/polkit-1/actions

clean:
	rm -f mobarchy-fingerprint

.PHONY: install clean
