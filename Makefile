
PACKAGES= libevdev
PKGCONFIG= pkg-config
OPTIMFLAGS= -O2
CFLAGS= -Wall -Wextra $(OPTIMFLAGS) $(shell $(PKGCONFIG) --cflags $(PACKAGES))
LDFLAGS= $(shell $(PKGCONFIG) --libs $(PACKAGES))
.PHONY: all clean

all: home-row-fu

home-row-fu: home-row-fu.o libtoml.a

libtoml.a: lib/toml.o
	ar rcs $@ $^

clean:
	rm *.o *.a lib/*.o home-row-fu
