# RK3566 U-Boot patches

Imported from ROCKNIX RK3566 Generic U-Boot support:

- Repository: https://github.com/ROCKNIX/distribution
- Source path: `projects/ROCKNIX/devices/RK3566/packages/u-boot-Generic/patches/`

These patches fix Anbernic RGxx3/RG Arc panel detection with mainline ATF and align the detected `fdtfile` names with boot partition DTB paths.

Applied automatically by `minime/build/mkbootloader.sh` during the bootloader build (any `*.patch` in this directory).
