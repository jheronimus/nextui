# Linux Kernel Patches for RK3326 Handhelds

These patches are applied to the mainline Linux kernel (dynamically resolved to `7.1.x`) during both the Alpine and Buildroot compilation processes.

## Source Reference
The hardware and board support patches are sourced from the **[ROCKNIX/distribution](https://github.com/ROCKNIX/distribution)** project.

### Upstream Build System References
For future audits, the following upstream files define how kernel patches are mapped, versioned, and applied:
- **Project Overrides**: [projects/ROCKNIX/packages/linux/package.mk](https://github.com/ROCKNIX/distribution/blob/next/projects/ROCKNIX/packages/linux/package.mk) (defines the kernel version and patch subdirectories)
- **Patch Engine**: [scripts/unpack](https://github.com/ROCKNIX/distribution/blob/next/scripts/unpack) (executes the directory traversal and patch applications)

---

## Active Patches
All imported patches are applied exactly as they are configured in Rocknix's RK3326 build, enabling standard screen panel timings, unique GPIO guidelines, Realtek SDIO Wi-Fi, and standard USB configurations for Anbernic RG351 devices.
