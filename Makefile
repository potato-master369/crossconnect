CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk4) -Os -ffast-math -fno-exceptions -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_80 -std=c99
LDFLAGS = -Wl,--gc-sections
LDLIBS = $(shell pkg-config --libs gtk4)

TARGETS = crossconnect
OBJ = main.o config.o

PREFIX ?= /usr/local
DESKTOPDIR ?= $(PREFIX)/share/applications

all: $(TARGETS)

crossconnect: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

main.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

config.o: src/config.c src/config.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGETS)

install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 crossconnect $(DESTDIR)$(PREFIX)/bin/crossconnect
	install -d $(DESTDIR)$(DESKTOPDIR)
	install -m 644 ass/crossconnect.desktop $(DESTDIR)$(DESKTOPDIR)/crossconnect.desktop

