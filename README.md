# NextUI on Alpine

**NextUI for Alpine** is an optimized, minimal distribution of the [NextUI](https://github.com/LoveRetro/NextUI) retro handheld launcher tailored specifically for the Anbernic RG35XX-SP (v1) and RG Arc D. Built upon an Alpine Linux foundation with mainline Linux kernels, it eliminates vendor BSP bloat and boots directly into NextUI using a lightweight musl userspace and open-source Panfrost DRM/KMS hardware acceleration.

The platform architecture utilizes a unified `alpine` target powered by a dynamic devicetree and sysfs trait system. At boot, hardware traits auto-detect the host device, configure screen geometry and rotation, bind physical inputs (including the Arc D 6-button face layout and RG35XX-SP clamshell lid sensor for instant sleep/wake), and parameterize ALSA/tinyalsa audio routes without requiring separate OS builds for each handheld.

All image assembly and payload compilation are orchestrated via [Taskfile](Taskfile.yml) and automated native aarch64 GitHub Actions workflows. Releases produce flashable SD card images (`.img.zst`) along with self-contained OTA update packages (`.tar.zst`), enabling direct on-device updates via the built-in OTA updater.
