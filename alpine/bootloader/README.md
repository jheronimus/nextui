# Bootloader blobs

These are **not built by this repo**. They are vendored binaries, committed so
that `task build` produces a flashable image without a separate toolchain.

We do not compile u-boot. Minime does, in CI, and these files are the output of
that job.

## Provenance

| | |
|---|---|
| Source repo | [`jheronimus/minime`](https://github.com/jheronimus/minime) |
| Workflow | `Nightly Rebuilds` |
| Run | [36409045900](https://github.com/jheronimus/minime/actions/runs/36409045900) (2026-09-28) |
| Artifact | `bootloader-out` |
| Minime commit | `02bcb54d3635224574994e7f9d0c6bd64925d036` |
| Variant taken | the `alpine` jobs, not the `buildroot` ones |

Each `bootloader-out` artifact contains every board Minime builds, so one
download covered both of ours. Only the two boards we target were kept.

## sha256

```
e64a358992760e5fc8f132e11848f7236050146ebab34c387b5fe6b918c608ec  h700/out/u-boot-sunxi-with-spl.bin
9a04db024daf69a50d98ba87ac45dbf9273a3e6e93b962be712889bf5443d2c5  h700/out/u-boot-sunxi-with-spl-ddr3.bin
053c0ea9a65e287b903033d53bdf806df387a663ef4d778155f474407b163e54  rk3566/out/idbloader.img
b5b180b03bcd58bdd546e0311e6daf6bbb14e888c33aff1783063fbba0f3629d  rk3566/out/u-boot.itb
```

The two h700 files are the same size but not the same bytes. Minime builds both
from the same u-boot tree; `-ddr3` is the variant for DDR3 boards. RG35XX SP
(RG35XX-SP-v1) is an LPDDR3 part, and `alpine/image/build.sh` stages the `-ddr3`
image as `.minime/u-boot-ddr3.bin` on the SD card for the bootloader to pick up.
Both are kept so the same image works on either memory type.

To refresh after a Minime u-boot change, re-download the artifact and re-check
the hashes above.

## Licensing

This is the part to be careful about.

- **u-boot** itself is GPL-2.0. The `u-boot.itb` and the h700
  `u-boot-sunxi-with-spl*.bin` are produced by compiling u-boot, so they are
  GPL-2.0 covered as a whole. Their corresponding source is in Minime's repo and
  in the u-boot upstream tree.
- **rk3566** additionally embeds blobs from Rockchip's `rkbin` (DDR init
  `rk3566_ddr_1056MHz_v1.25.bin`, miniloader, BL31). Rockchip ships these under
  its own terms, reproduced verbatim in `rk3566/LICENSE.rkbin` — an "AS IS"
  warranty disclaimer, **not** a standard open-source licence.
- **h700** embeds Allwinner's SPL and DDR init from their BSP. No licence file
  ships with the artifact; Allwinner's BSP is GPL-2.0, but the vendor terms for
  the prebuilt blobs are not stated.

So the rk3566 and h700 images are not cleanly redistributable. That is fine for
a private repo and it is what every RG35XX SP and Arc-D image project does, but
these files should not be published as part of a public release without checking
with the vendors. GPL-2.0 requires offering the corresponding source for the
u-boot portion; Minime's commit above identifies a reproducible tree.

`LICENSE.rkbin` is included verbatim as Rockchip requires.
