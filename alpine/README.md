# alpine/ — the Alpine foundation

Everything needed to build a bootable Alpine system for the supported boards:
the kernel and its patches, DTBs and overlays, firmware blobs, bootloader inputs,
the package list, the builder container, and the scripts that assemble a flashable
image.

Nothing in here knows about NextUI. It produces an OS.

```
alpine/
  Makefile          orchestration for the foundation, driven by the root Taskfile
  packages.txt      the rootfs package selection, shared by every board
  container/        the aarch64 musl builder image
  aports/           APKBUILDs for what Alpine does not ship: tinykernel, fatresize, mdnsd
  boards/
    common/         firmware, overlays, base kernel configs, boot.cmd
    h700/           RG35XX SP v1: kernel patches, config, firmware
    rk3566/         RG ARC-D: kernel patches, config, dtbo overlays
    gentraits.sh    derives the trait registry and DTB list from the hardware
    check-*.sh      consistency checks, run by `task validate`
  scripts/          build.sh, post-build.sh, system-image.sh
  image/            genimage packaging into .img.zst
  check-upstream.sh enforces that no upstream NextUI file is ever edited
```

The device trait registry is **not** here. It belongs to the NextUI platform
port in `workspace/alpine/boards/`, because it is the port's runtime input. The
foundation describes the hardware; the port consumes it.

## Invoking it

Use the root Taskfile, not this makefile directly:

```
task builder            # build the toolchain container
task components BOARD=h700    # kernel + rootfs
task image BOARD=h700         # package the image
task build BOARD=h700         # all of the above, plus the NextUI payload
```
