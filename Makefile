CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags gl glu glx x11 xcb)
MACHINE = $(shell gcc -dumpmachine)
X11XCB = $(shell test -e /usr/lib/$(MACHINE)/libX11-xcb.so && echo -lX11-xcb || echo /usr/lib/$(MACHINE)/libX11-xcb.so.1)
LIBS = $(shell pkg-config --libs gl glu glx x11 xcb) $(X11XCB) -lm

xcb-clock: clock.c
	$(CC) $(CFLAGS) -o $@ clock.c $(LIBS)

clean:
	rm -f xcb-clock
