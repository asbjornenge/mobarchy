# mobarchy-location: the phone's GNSS for Linux (GeoClue's NMEA source), from the Android
# container's GNSS HAL. Package: tools/pkg-build os/location
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags libgbinder gio-unix-2.0)
LDLIBS = $(shell pkg-config --libs libgbinder gio-unix-2.0) -lm

mobarchy-location: mobarchy-location.c

install: mobarchy-location
	install -Dm755 mobarchy-location mobarchy-location-psds -t $(DESTDIR)/usr/lib/mobarchy
	install -Dm644 mobarchy-location.service mobarchy-location-psds.service mobarchy-location-psds.timer \
		-t $(DESTDIR)/usr/lib/systemd/system
	install -Dm644 90-mobarchy.conf -t $(DESTDIR)/etc/geoclue/conf.d
	install -Dm644 mobarchy-location.sysusers $(DESTDIR)/usr/lib/sysusers.d/mobarchy-location.conf

clean:
	rm -f mobarchy-location

.PHONY: install clean
