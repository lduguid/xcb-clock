# xcb-clock

This is not a game SDK. It is a finished X11 demo: an analog or digital clock in front of a slowly tumbling GL cube. You own `clock.c` end to end — Display, GLX, XCB, redraw.

Fork it if you want another “one window, GL overlay, X events on the side” toy. There is no `tick` API to implement.

## Build

Linux / WSL only:

```bash
make
./xcb-clock
```

Needs X11, XCB, GL, GLU, GLX (`pkg-config`).

## What you are looking at

- Analog face or digital digits, toggled with the on-screen switch (click) or keys.
- The cube is GL behind the 2D overlay. Arrow keys add spin.
- Time comes from the machine clock each frame.

## Keys

| Key | Action |
|-----|--------|
| Click the toggle | Analog ↔ digital |
| Arrows | Nudge cube rotation |
| Q / Esc / close | Quit |

If you change the demo, stay in `clock.c`. Window size, colors, and the cube live in the same file as the event loop.
