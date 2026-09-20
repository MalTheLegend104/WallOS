# Kilo

Original Source: https://github.com/antirez/kilo/tree/master

This has been heavily modified to work in the kernel environment in WallOS.

Only the `LICENSE` and `kilo.c` have code that fall under Kilo's license.

`makefile` and this `README.md` fall under WallOS's license.

## Changes

A majority of the changes to kilo are in the following departments:

- VT100 sequences have been removed and we control the display/terminal directly
- Key handling is more streamlined since we don't have to deal with VT100, but we do have to deal with `wallos_input_event_t`.
- Terminal Raw Mode isn't a thing, we control everything manually anyway.
- Some improvements relating to when to draw to the screen, this is mostly due to how we have to handle the display directly.
