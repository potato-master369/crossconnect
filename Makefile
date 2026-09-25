CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk4) -Os -ffast-math -fno-exceptions -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_80 -std=c99
LDFLAGS = -Wl,--gc-sections
LDLIBS = $(shell pkg-config --libs gtk4)

TARGETS = crossconnect
OBJ = main.o

all: $(TARGETS)

crossconnect: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

main.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGETS)
