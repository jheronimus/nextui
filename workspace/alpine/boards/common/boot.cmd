setenv bootargs "console=tty1 root=/dev/ram0 rdinit=/init rootwait pm_async=off loglevel=3 printk.devkmsg=on consoleblank=0 vt.global_cursor_default=0 rtw88_core.disable_lps_deep=Y drm_kms_helper.drm_fbdev_overalloc=200 @BOOTARGS@"
@EXTRA_ENV@

setenv bootdevtype mmc
setenv bootdevnum 0:1
if test -n "${devtype}" -a -n "${devnum}" -a -n "${distro_bootpart}"; then
	setenv bootdevtype "${devtype}"
	setenv bootdevnum "${devnum}:${distro_bootpart}"
fi

if test -z "${ramdisk_addr_r}"; then
	setenv ramdisk_addr_r 0x46000000
fi

if test -z "${fdt_addr_r}"; then
	setenv fdt_addr_r 0x4fa00000
fi

setenv default_device "@DEFAULT_DEVICE@"
setenv device auto
if fatload ${bootdevtype} ${bootdevnum} ${scriptaddr} .minime/config/device.cfg; then
	env import -t ${scriptaddr} ${filesize}
fi

if test "${device}" = "auto"; then
	if test -n "${fdtfile}"; then
		setenv device "${fdtfile}"
	else
		setenv device "${default_device}"
	fi
fi

# Initialize boot log at 0x4ff00000 with "----"
mw.l 0x4ff00000 0x2d2d2d2d 2

if fatload ${bootdevtype} ${bootdevnum} ${kernel_addr_r} .minime/kernel; then
	echo "Loaded .minime/kernel"
	mw.b 0x4ff00000 0x4b
else
	echo "Failed to load .minime/kernel"
	mw.b 0x4ff00000 0x6b
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 1
	sleep 5
	reset
fi

if fatload ${bootdevtype} ${bootdevnum} ${fdt_addr_r} .minime/devices/${device}; then
	echo "Loaded .minime/devices/${device}"
	mw.b 0x4ff00001 0x46
else
	echo "Device DTB not found, falling back to .minime/dtb"
	if fatload ${bootdevtype} ${bootdevnum} ${fdt_addr_r} .minime/dtb; then
		echo "Loaded .minime/dtb"
		mw.b 0x4ff00001 0x46
	else
		echo "Failed to load .minime/dtb"
		mw.b 0x4ff00001 0x66
		fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 2
		sleep 5
		reset
	fi
fi

if fatload ${bootdevtype} ${bootdevnum} ${ramdisk_addr_r} .minime/initramfs; then
	setenv initrd_size ${filesize}
	echo "Loaded .minime/initramfs (size: ${initrd_size})"
	mw.b 0x4ff00002 0x49
else
	echo "Failed to load .minime/initramfs"
	mw.b 0x4ff00002 0x69
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 3
	sleep 5
	reset
fi

fdt addr ${fdt_addr_r}
if test $? -eq 0; then
	mw.b 0x4ff00003 0x41
else
	echo "Failed: fdt addr"
	mw.b 0x4ff00003 0x61
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 4
	sleep 5
	reset
fi

fdt resize
if test $? -eq 0; then
	mw.b 0x4ff00004 0x52
else
	echo "Failed: fdt resize"
	mw.b 0x4ff00004 0x72
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 5
	sleep 5
	reset
fi

if test -z "${undervolt}"; then
	setenv undervolt "l1"
fi

if test "${undervolt}" = "l1" -o "${undervolt}" = "l2" -o "${undervolt}" = "l3"; then
	if fatload ${bootdevtype} ${bootdevnum} ${scriptaddr} .minime/overlays/rk3566-undervolt-cpu-${undervolt}.dtbo; then
		fdt apply ${scriptaddr}
		echo "Applied CPU undervolt ${undervolt} overlay"
	fi
fi

fdt set /chosen bootargs "${bootargs}"
if test $? -eq 0; then
	mw.b 0x4ff00005 0x53
else
	echo "Failed: fdt set bootargs"
	mw.b 0x4ff00005 0x73
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 6
	sleep 5
	reset
fi

fdt chosen ${ramdisk_addr_r} ${initrd_size}
if test $? -eq 0; then
	mw.b 0x4ff00006 0x43
else
	echo "Failed: fdt chosen"
	mw.b 0x4ff00006 0x63
	fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 7
	sleep 5
	reset
fi

# Write full log before boot
mw.b 0x4ff00007 0x42
fatwrite ${bootdevtype} ${bootdevnum} 0x4ff00000 .minime/boot.log 8

booti ${kernel_addr_r} ${ramdisk_addr_r}:${initrd_size} ${fdt_addr_r}
sleep 5
reset
