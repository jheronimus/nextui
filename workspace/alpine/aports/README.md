# alpine/aports

APKBUILDs for the packages this repo builds from source, cross-compiled to
`aarch64-linux-musl` and installed into the rootfs.

| Package | Source | Why we build it |
|---|---|---|
| `tinykernel` | linux 7.1.5 + `alpine/boards/*/patches/linux/` | The kernel and the device DTBs. Not installed into the rootfs; staged straight onto the SD image. |
| `fatresize` | [ya-mouse/fatresize](https://github.com/ya-mouse/fatresize) | Grows the FAT32 userdata partition on first boot. |
| `mdnsd` | [troglobit/mdnsd](https://github.com/troglobit/mdnsd) | `.local` service discovery so the device is reachable by name. |

`fatresize` and `mdnsd` are unmodified upstream; the APKBUILD is all we carry,
and each pins a sha512 of the published tarball.

## Deliberately not here

Minime's `world-common` also lists `bootsplash` and `remote`. **We do not build
them, and that is a licensing decision, not an oversight.**

Minime's repository has no `LICENSE` file and no license metadata on GitHub.
Their `src/bootsplash` (a framebuffer splash daemon) and `src/remote` (remote
screenshot and input injection) are their own C code, and their
`aports/*/APKBUILD` builds them by reaching into `src/`. Vendoring that source
into this repo would mean shipping code with no license grant behind it. The
firmware blobs in `alpine/boards/common/firmware/` are fine -- they are upstream
Realtek and `wireless-regdb` releases, not Minime's -- and the board configs
are data, but the C is not ours to take.

Nothing breaks:

- `etc/init.d/bootsplash` and `initramfs-init.sh` both guard with
  `if [ -x /usr/bin/bootsplash ]`, so the service starts, finds no binary, and
  the boot continues without a splash.
- Nothing in the overlay invokes `/usr/bin/remote` at all; it is a
  developer diagnostic.

Both are additive. If you want them, the clean path is to ask the Minime author
for a license grant, or to write your own — `bootsplash` is a few hundred lines
against a framebuffer plus the trait registry, and the trait registry is
already on the device at `/usr/share/minime/traits`. Either way, when you add
one, also add it to the *Local packages* section of `alpine/packages.txt`;
`check-boards.sh` fails if the two lists disagree.

## The kernel version is load-bearing

`tinykernel/APKBUILD` pins `pkgver=7.1.5`. The 52 vendored patches in
`alpine/boards/{h700,rk3566}/patches/` are written against it. Bumping the
kernel without re-auditing the series is the most likely way to get a build
failure that looks unrelated. See `alpine/boards/patches/README.md`.
