#!/bin/sh
# shellcheck shell=sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# Single-owner backlight: set to max from sysfs before bootsplash
for max_bl in /sys/class/backlight/*/max_brightness; do
	if [ -r "$max_bl" ]; then
		bl_dir="$(dirname "$max_bl")"
		if [ -w "$bl_dir/brightness" ]; then
			cat "$max_bl" >"$bl_dir/brightness" 2>/dev/null || true
		fi
	fi
done

BOOTSPLASH_PID=""
if [ -x /usr/bin/bootsplash ]; then
	/usr/bin/bootsplash --persist &
	BOOTSPLASH_PID=$!
fi

CARD_DEV=""
BOOT_LOG_DIR="/mnt/card"

log_console() {
	echo "$*"
}

log_card() {
	echo "$*"
	if [ -w "${BOOT_LOG_DIR}" ] 2>/dev/null; then
		echo "[INITRAMFS $(date -u +'%T' 2>/dev/null || date 2>/dev/null || true)] $*" >>"${BOOT_LOG_DIR}/boot.log" 2>/dev/null || true
		sync 2>/dev/null || true
	fi
}

run_fat_fsck() {
	/sbin/fsck.fat -a "$CARD_DEV"
	FSCK_RC=$?
}

finish_card_mount() {
	if ! mount -t vfat "$CARD_DEV" /mnt/card 2>/dev/null; then
		log_console "ERROR: failed to mount repaired FAT partition $CARD_DEV"
		exec sh
	fi
	[ -f /mnt/card/.minime/system ] || {
		umount /mnt/card 2>/dev/null || true
		return 1
	}

	case "$FSCK_RC" in
	0 | 1)
		log_card "[INITRAMFS] FAT fsck completed (exit $FSCK_RC)"
		;;
	*)
		log_card "[INITRAMFS] WARNING: FAT fsck exited with code $FSCK_RC"
		;;
	esac
	rm -f /mnt/card/FSCK*.REC /mnt/card/fsck*.rec 2>/dev/null || true
	log_card "[INITRAMFS] Mounted MINIME FAT partition on $CARD_DEV"
	return 0
}

mkdir -p /mnt/card

# Wait for Linux to enumerate SD/eMMC devices and mount the partition.
log_console "Waiting for block devices..."
for _i in 1 2 3 4 5 6 7 8 9 10; do
	for dev in /dev/mmcblk*p1 /dev/vd*1 /dev/sd*1; do
		[ -b "$dev" ] || continue
		if [ "$(blkid -s LABEL -o value "$dev" 2>/dev/null)" = "minime" ]; then
			CARD_DEV="$dev"
			log_console "Checking MINIME FAT filesystem on $CARD_DEV..."
			run_fat_fsck
			finish_card_mount && break
			CARD_DEV=""
			continue
		fi

		# BusyBox builds without usable blkid support fall back to the original
		# marker-based probe, then fsck the identified partition before remounting.
		if mount -t vfat "$dev" /mnt/card 2>/dev/null; then
			if [ -f /mnt/card/.minime/system ]; then
				CARD_DEV="$dev"
				log_card "[INITRAMFS] Found MINIME FAT partition on $CARD_DEV; checking filesystem..."
				umount /mnt/card 2>/dev/null || {
					log_card "ERROR: failed to unmount $CARD_DEV before fsck"
					exec sh
				}
				run_fat_fsck
				finish_card_mount && break
				CARD_DEV=""
				continue
			fi
			umount /mnt/card 2>/dev/null || true
		fi
	done
	[ -n "$CARD_DEV" ] && break
	sleep 1
done

if [ -z "$CARD_DEV" ]; then
	log_console "ERROR: failed to mount a MINIME FAT partition"
	exec sh
fi

log_card "[INITRAMFS] Initialized persistent logging on $CARD_DEV"

# H700 DDR3/DDR4 runtime detection.
# The default U-Boot at SD card offset 8K is built for LPDDR4 (DCDC3=1100mV).
# If this device has LPDDR3 memory (DCDC3=1200mV), swap in the DDR3 U-Boot
# binary stored on the FAT partition and reboot so it takes effect.
if [ -f /mnt/card/.minime/u-boot-ddr3.bin ] && [ ! -f /mnt/card/.minime/.ddr3-swapped ]; then
	dram_uv=""
	for r in /sys/class/regulator/regulator.*/; do
		if [ "$(cat "$r/name" 2>/dev/null)" = "vdd-dram" ]; then
			dram_uv="$(cat "$r/microvolts" 2>/dev/null)"
			break
		fi
	done
	if [ "$dram_uv" = "1200000" ]; then
		log_card "[INITRAMFS] LPDDR3 detected (DCDC3=${dram_uv}uV), swapping U-Boot binary..."
		DISK_DEV="${CARD_DEV%p1}"
		if dd if=/mnt/card/.minime/u-boot-ddr3.bin of="$DISK_DEV" bs=1k seek=8 2>/dev/null; then
			touch /mnt/card/.minime/.ddr3-swapped
			sync
			log_card "[INITRAMFS] DDR3 U-Boot written to ${DISK_DEV}, rebooting..."
			umount /mnt/card 2>/dev/null || true
			reboot -f
		else
			log_card "[INITRAMFS] WARNING: failed to write DDR3 U-Boot, continuing with DDR4"
		fi
	fi
fi

# First-boot hardware probe.
if [ -f /mnt/card/.minime/config/first_boot_probe ]; then
	log_card "[INITRAMFS] Running first-boot hardware probe..."
	mount -o remount,rw /mnt/card

	if [ -f /sbin/first-boot-probe.sh ]; then
		sh /sbin/first-boot-probe.sh
	fi

	rm -f /mnt/card/.minime/config/first_boot_probe
	umount /mnt/card
	reboot -f
fi

# Grow partition 1 and recreate FAT32 at full card size.
# FAT32 cannot be grown in place reliably (fatresize has geometry bugs), so on
# first boot we stage the seeded FAT contents into RAM, wipe the partition with
# mkfs.vfat at its full resized size, and restore the contents. This mirrors
# the approach used by EmuELEC, dArkOS, and similar single-FAT32 firmware.
if [ -f /mnt/card/.minime/config/first_boot_expand ]; then
	log_card "[INITRAMFS] Expanding SD card on $CARD_DEV..."
	DISK_DEV="${CARD_DEV%p1}"
	PART_NUM="${CARD_DEV##*p}"

	# Stage all FAT contents into a RAM-backed tmpfs BEFORE resizing the
	# partition. parted resizepart + partprobe re-read the partition table and
	# invalidate a still-mounted vfat (the kernel flips it read-only), so the
	# seed must be copied while the card is still healthy. FAT32 cannot be
	# grown in place, so the volume is wiped and recreated at full size below.
	STAGE=/tmp/stage
	mkdir -p "$STAGE"
	if ! mount -t tmpfs -o size=512M tmpfs "$STAGE" 2>/dev/null; then
		log_card "ERROR: failed to mount staging tmpfs at $STAGE"
		exec sh
	fi
	log_card "[INITRAMFS] Staging FAT contents into $STAGE..."
	cp -a /mnt/card/. "$STAGE"/ 2>/dev/null || {
		log_card "ERROR: failed to stage FAT contents"
		exec sh
	}

	# Release the vfat mount before re-reading the partition table so partprobe
	# re-reads the grown partition cleanly instead of invalidating the mount.
	umount /mnt/card 2>/dev/null || true
	# Point logging at the staged copy (which is restored onto the card below)
	# so boot.log continuity is kept across the unmounted window.
	BOOT_LOG_DIR="$STAGE"

	# parted and mkfs.vfat are not in the initramfs to avoid dynamic linking
	# complexity. We mount the EROFS system image from the staged copy (it must
	# survive the reformat) and run them via chroot with the device nodes,
	# proc, and sysfs bind-mounted.
	mkdir -p /mnt/system
	if ! mount -t erofs -o loop,ro "$STAGE/.minime/system" /mnt/system 2>/dev/null; then
		log_card "ERROR: failed to mount /mnt/system for partition expansion"
		exec sh
	fi
	mount --bind /dev /mnt/system/dev
	mount --bind /proc /mnt/system/proc
	mount --bind /sys /mnt/system/sys

	PARTED_OUT="$(chroot /mnt/system parted -s -f "$DISK_DEV" resizepart "$PART_NUM" 100% 2>&1)"
	PARTED_RC=$?
	chroot /mnt/system partprobe "$DISK_DEV" 2>/dev/null || true

	sleep 1

	PART_SECTORS="$(cat "/sys/block/${DISK_DEV##*/}/${CARD_DEV##*/}/size" 2>/dev/null || echo 0)"
	log_card "[INITRAMFS] parted output: ${PARTED_OUT} (exit ${PARTED_RC})"
	log_card "[INITRAMFS] $CARD_DEV: sectors=${PART_SECTORS}"

	if [ "$PARTED_RC" -ne 0 ]; then
		log_card "ERROR: failed to expand partition $PART_NUM on $DISK_DEV"
		exec sh
	fi

	# Wipe and recreate FAT32 at the full resized partition size.
	log_card "[INITRAMFS] Recreating FAT32 on $CARD_DEV..."
	MKFS_OUT="$(chroot /mnt/system mkfs.vfat -F 32 -s 32 -n minime "$CARD_DEV" 2>&1)"
	MKFS_RC=$?
	log_card "[INITRAMFS] mkfs.vfat output: ${MKFS_OUT} (exit ${MKFS_RC})"
	if [ "$MKFS_RC" -ne 0 ]; then
		log_card "ERROR: failed to recreate $CARD_DEV"
		exec sh
	fi

	mount -t vfat "$CARD_DEV" /mnt/card 2>/dev/null || {
		log_card "ERROR: failed to remount recreated $CARD_DEV"
		exec sh
	}

	log_card "[INITRAMFS] Restoring staged FAT contents..."
	cp -a "$STAGE"/. /mnt/card/ 2>/dev/null || {
		log_card "ERROR: failed to restore FAT contents"
		exec sh
	}
	BOOT_LOG_DIR="/mnt/card"

	# Re-hide the system directories (mkfs.vfat resets the hidden attribute).
	# Run via chroot into the EROFS system (its busybox has the fatattr applet).
	# The EROFS has /mnt/sdcard (the post-switch_root mount point) but the card
	# is currently mounted at /mnt/card in the initramfs; bind-mount it there so
	# the paths resolve inside the chroot.
	mount --bind /mnt/card /mnt/system/mnt/sdcard 2>/dev/null || true
	chroot /mnt/system fatattr +h /mnt/sdcard/.minime 2>/dev/null || true
	chroot /mnt/system fatattr +h /mnt/sdcard/.system 2>/dev/null || true
	umount /mnt/system/mnt/sdcard 2>/dev/null || true

	umount "$STAGE" 2>/dev/null || true
	rm -f /mnt/card/.minime/config/first_boot_expand
	sync

	log_card "[INITRAMFS] Partition recreation successful. Rebooting..."
	umount /mnt/card 2>/dev/null || true
	reboot -f
fi

log_card "[INITRAMFS] Mounting EROFS system image..."
mkdir -p /mnt/system
if ! mount -t erofs -o loop,ro /mnt/card/.minime/system /mnt/system; then
	log_card "ERROR: failed to mount /mnt/card/.minime/system"
	exec sh
fi
log_card "[INITRAMFS] EROFS system image mounted successfully."

# Pre-emptively fix clock skew by advancing the system time to the rootfs build time
# if the RTC woke up in the past (e.g. 2017). This prevents OpenRC from printing
# "WARNING: clock skew detected!" during boot before NTP syncs.
BUILD_TIME=$(date -r /mnt/system/.build_time +%s 2>/dev/null || date -r /mnt/system/bin/busybox +%s 2>/dev/null || echo 0)
CUR_TIME=$(date +%s 2>/dev/null || echo 0)
if [ "$BUILD_TIME" -gt "$CUR_TIME" ]; then
	log_card "[INITRAMFS] Advancing system time to $BUILD_TIME to prevent clock skew"
	date -s "@$BUILD_TIME" >/dev/null 2>&1 || true
fi

# Establish a per-boot log directory.  Boot-id = YYYYmmdd-N, where N is a
# per-day monotonic counter stored in .minime/logs/.seq (format "DATE N").
# The time-of-day is deliberately omitted: the RTC wakes in the past, so
# HHMMSS would be wrong or repeated across cold boots.  Runs after the clock
# skew fix above so the date (and thus the 7-day prune via FAT mtime) is sane.
LOGS_DIR="/mnt/card/.minime/logs"
mkdir -p "${LOGS_DIR}"
seq_date=""
seq_num="0"
if [ -f "${LOGS_DIR}/.seq" ]; then
	read -r seq_date seq_num <"${LOGS_DIR}/.seq" 2>/dev/null || true
fi
today="$(date +%Y%m%d 2>/dev/null || echo 19700101)"
if [ "${seq_date}" != "${today}" ]; then
	seq_num="1"
else
	seq_num="$((seq_num + 1))"
fi
BOOT_ID="${today}-${seq_num}"
printf '%s %s\n' "${today}" "${seq_num}" >"${LOGS_DIR}/.seq"

# Adopt U-Boot's stage markers (.minime/boot.log) and any early initramfs
# boot.log, then point subsequent logging at the per-boot dir.
BOOT_LOG_DIR="${LOGS_DIR}/${BOOT_ID}"
mkdir -p "${BOOT_LOG_DIR}"
if [ -f /mnt/card/.minime/boot.log ]; then
	cat /mnt/card/.minime/boot.log >>"${BOOT_LOG_DIR}/boot.log" 2>/dev/null || true
fi
if [ -f /mnt/card/boot.log ]; then
	cat /mnt/card/boot.log >>"${BOOT_LOG_DIR}/boot.log" 2>/dev/null || true
	rm -f /mnt/card/boot.log 2>/dev/null || true
fi
printf '%s\n' "${BOOT_ID}" >"${LOGS_DIR}/current"
sync
log_card "[INITRAMFS] Boot log: ${BOOT_ID}"

# Hard check that target init exists and is executable before switch_root
if [ ! -x /mnt/system/sbin/init ]; then
	log_card "ERROR: /mnt/system/sbin/init is missing or not executable"
	ls -la /mnt/system/sbin/init 2>&1 || true
	exec sh
fi

if [ -n "$BOOTSPLASH_PID" ]; then
	kill "$BOOTSPLASH_PID" 2>/dev/null || true
	wait "$BOOTSPLASH_PID" 2>/dev/null || true
fi

log_card "[INITRAMFS] Moving mounts and switching root to /mnt/system..."
mount -o move /sys /mnt/system/sys
mount -o move /proc /mnt/system/proc
mount -o move /dev /mnt/system/dev
mount -o move /mnt/card /mnt/system/mnt/sdcard

exec switch_root /mnt/system /sbin/init
