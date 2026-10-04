# mobarchy-sensor-proxy (ADR 0020). Build: make; install: make install DESTDIR=...
PKGS = gio-2.0 gio-unix-2.0
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

mobarchy-sensor-proxy: mobarchy-sensor-proxy.c ../protocol.h
	$(CC) $(CFLAGS) -o $@ mobarchy-sensor-proxy.c $(LDLIBS)

install: mobarchy-sensor-proxy
	install -Dm755 mobarchy-sensor-proxy -t $(DESTDIR)/usr/lib/mobarchy
	install -Dm644 mobarchy-sensor-proxy.service -t $(DESTDIR)/usr/lib/systemd/system
	install -Dm644 mobarchy-sensor-proxy.sysusers $(DESTDIR)/usr/lib/sysusers.d/mobarchy-sensor-proxy.conf
	install -Dm644 net.hadess.SensorProxy.conf -t $(DESTDIR)/usr/share/dbus-1/system.d

clean:
	rm -f mobarchy-sensor-proxy

.PHONY: install clean
