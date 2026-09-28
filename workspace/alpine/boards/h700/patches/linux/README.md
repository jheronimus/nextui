# Linux Kernel Patches for H700 Handhelds

These patches are applied to the mainline Linux kernel (dynamically resolved to `7.1.x`) during both the Alpine and Buildroot compilation processes.

## Source Reference
The hardware and board support patches are sourced from the **[ROCKNIX/distribution](https://github.com/ROCKNIX/distribution)** project.

### Upstream Build System References
For future audits, the following upstream files define how kernel patches are mapped, versioned, and applied:
- **Project Overrides**: [projects/ROCKNIX/packages/linux/package.mk](https://github.com/ROCKNIX/distribution/blob/next/projects/ROCKNIX/packages/linux/package.mk) (defines the kernel version and patch subdirectories)
- **Patch Engine**: [scripts/unpack](https://github.com/ROCKNIX/distribution/blob/next/scripts/unpack) (executes the directory traversal and patch applications)

---

## Manual Corrections
One patch from the original Rocknix collection had to be manually corrected to apply cleanly under Buildroot's strict `--fuzz=0` requirement:
1. **[0018-rg35xx-2024-enable-usb-otg.patch](0018-rg35xx-2024-enable-usb-otg.patch)** (Originally Rocknix `0152-rg35xx-2024-enable-usb-otg.patch`): Corrected empty context line space discrepancies.

## Upstream Tracking
The per-patch upstream status (which patches can be dropped as their content lands in mainline) is tracked in the kernel-patch manifest at [`minime/build/kernel-patch-manifest`](../../../build/kernel-patch-manifest) — the single source of truth for patch triage.

## Omitted Patches
The following patches from Rocknix's `H700` device package were omitted:
- **`0140-rg35xx-2024-use-rocknix-joypad-driver.patch`**: Omitted in favor of the standard mainline kernel input subsystems. Analog sticks on multiplexed-ADC devices (like RG35XX-H) are supported natively using built-in Linux drivers (`CONFIG_JOYSTICK_ADC`, `CONFIG_IIO_MUX`, `CONFIG_MUX_GPIO`, and `CONFIG_MULTIPLEXER`), which are enabled in our kernel configuration fragment.
- **`0144-Update-sun50i-h700-anbernic-rg35xx-h.dts.patch`**: Omitted as it modifies config for the out-of-tree Rocknix joypad driver.
- **`0150-add-forcefeedback.patch`**: Omitted as rumble support is not needed for the minimal baseline image.
- **`0203-sound-soc-Add-sunxi_v2-for-h616-ahub.patch.disabled`**: Omitted as it is disabled upstream in Rocknix.
