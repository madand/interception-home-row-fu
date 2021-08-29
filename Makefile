
PREFIX ?= /usr/local
COMPFLAGS = -O2 -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE
PACKAGES = libevdev
PKGCONFIG = pkg-config
CFLAGS = $(COMPFLAGS) $(shell $(PKGCONFIG) --cflags $(PACKAGES))
LDFLAGS = $(shell $(PKGCONFIG) --libs $(PACKAGES))

all: home-row-fu

home-row-fu: home-row-fu.o libtoml.a

libtoml.a: lib/toml.o
	ar rcs $@ $^

install:
	install -m 755 home-row-fu $(DESTDIR)$(PREFIX)/bin/

install-config-file:
	install -m 644 home-row-fu.toml $(DESTDIR)$(PREFIX)/etc/

clean:
	rm *.o *.a lib/*.o home-row-fu

.PHONY: all install install-config-file clean
