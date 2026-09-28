# Linux Kernel Patches for RK3566 Handhelds

These patches are applied to the mainline Linux kernel (dynamically resolved to `7.1.x`) during both the Alpine and Buildroot compilation processes.

## Source Reference
The hardware and board support patches are sourced from the **[ROCKNIX/distribution](https://github.com/ROCKNIX/distribution)** project.

### Upstream Build System References
For future audits, the following upstream files define how kernel patches are mapped, versioned, and applied:
- **Project Overrides**: [projects/ROCKNIX/packages/linux/package.mk](https://github.com/ROCKNIX/distribution/blob/next/projects/ROCKNIX/packages/linux/package.mk) (defines the kernel version and patch subdirectories)
- **Patch Engine**: [scripts/unpack](https://github.com/ROCKNIX/distribution/blob/next/scripts/unpack) (executes the directory traversal and patch applications)

---

## Mapped & Omitted Patches

### Omitted (Specific to other platforms)
To ensure a mainline-clean and highly optimized system, we omit several Powkiddy-specific patches carried by ROCKNIX:
* **`0005-arm64-dts-rockchip-fixup-anbernic-controls.patch`**: Omitted to use mainline kernel `gpio-keys` and standard `adc-joystick` input systems instead of Rocknix's custom out-of-tree joypad driver.
* **`0009-arm64-dts-rockchip-fix-shoulders-triggers-on-powkidd.patch`**: Powkiddy-specific hardware button fixes.
* **`0010-drm-panel-st7703-request-higher-pixelclock-for-RGB30.patch`**: Powkiddy RGB30 pixel clock adjustment.
* **`0012-arm64-dts-rockchip-update-powkiddy-x55-dts-to-suppor.patch`**: Powkiddy x55 specific patch.
* **`0016-arm64-dts-rockchip-add-device-tree-for-powkiddy-x35s.patch`**: Powkiddy x35s device tree addition.
* **`0018-arm64-dts-rockchip-add-device-tree-for-powkiddy-rgb2.patch`**: Powkiddy rgb20 device tree addition.

### Mainlined Upstream in 7.1.x (Dropped)
The following patches have been merged into mainline Linux and are dropped from our local series:
* **`0011-nvmem-rockchip-otp-Add-support-for-rk3568-otp.patch`**: Support for the RK3568 OTP controller is mainlined in `drivers/nvmem/rockchip-otp.c` (using `rk3568_data`).
* **`0012-arm64-dts-rockchip-rk3568-Add-otp-device-node.patch`**: The OTP device node for `rk356x-base.dtsi` is now natively present in mainline Linux 7.1.x, so the separate DTS addition is no longer needed.
* **`0017-arm64-dts-rockchip-Add-idle-states-for-rk356x.patch`**: CPU low-power idle sleep states are natively integrated in mainline `rk356x-base.dtsi` using the PSCI framework.

---

## 7.1.x Migration Adaptation Notes
During migration and compilation stabilization for `7.1.x`, some patches (e.g. SCMI clocks, battery descriptor name mappings, and the AW87391 shim driver) are kept to preserve compatibility with EmulationStation and the custom board-specific audio routing.
