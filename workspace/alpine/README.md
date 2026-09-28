# workspace/alpine/ — the NextUI platform port for Alpine

This is the NextUI side. It ports NextUI to the Alpine foundation that lives in
`alpine/` at the repo root: the platform shim, the device trait registry, and
the per-platform component overrides.

It contains no kernel, no rootfs and no packaging. The foundation builds the OS;
this builds the UI that runs on it.

```
workspace/alpine/
  platform/
    platform.c        the PLAT_* hooks NextUI's core calls into
    platform.h
    traits.c/.h       runtime trait parser (byte-identical to MinUI's)
    makefile.copy     assembles the compiled payload into build/
    makefile.env
  boards/<board>/traits/
    platform.ini      the platform's capabilities
    devices/*.ini     one per device: keys, audio, input, display, GPU
  libmsettings/       overrides workspace/desktop/libmsettings
  show/               overrides workspace/all/show2
  keymon/             overrides workspace/tg5040/keymon
```

## How it fits together

NextUI's own top-level `makefile` is not used, because it hardcodes its platform
list, has no `alpine` branch in `system`, and pulls a toolchain image that does
not exist. `alpine/Makefile` drives the build instead.

NextUI's *component* makefiles under `workspace/all/` are used unchanged as leaf
compilers. They resolve `../../$(PLATFORM)/platform/` generically, so pointing
them at `PLATFORM=alpine` is all it takes to pick this tree up. The `libmsettings`,
`show` and `keymon` directories here override their generic counterparts for this
platform only.

## Traits are runtime, not build-time

The device registry is compiled into nothing. `traits.c` reads it at boot from
`/mnt/sdcard/.minime/traits`, so a device can be added or corrected by editing an
ini file on the SD card. The immutable copy in the image is
`/usr/share/minime/traits`.

`alpine/boards/gentraits.sh` derives the registry from the hardware description in
the foundation, so the two cannot drift.

## Invoking it

Through the root Taskfile:

```
task payload     # compile and assemble the NextUI payload
task build       # foundation + this port, into a flashable image
```
