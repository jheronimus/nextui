#!/bin/sh
# shellcheck shell=sh
# NextUI Alpine system-image script: turn an assembled rootfs into the two
# artifacts the packager consumes -- system.erofs (the immutable root) and
# initramfs.img (the cpio that mounts the SD card and switch_roots into it).
#
# Derived from Minime's packages/components/alpine/scripts/system-image.sh.
# Changes: traits copied to /usr/share/minime/, and the bootsplash copy is now
# optional (we do not build it -- see alpine/aports/README.md).

set -eu

usage() {
	echo "Usage: ${0##*/} -b BOARD_NAME" >&2
}

BOARD_NAME=""
opts="$(getopt -n "${0##*/}" -o b: -- "$@")" || exit $?
eval set -- "$opts"
while true; do
	case "$1" in
	-b)
		BOARD_NAME="$2"
		shift 2
		;;
	--)
		shift
		break
		;;
	*)
		usage
		exit 1
		;;
	esac
done

if [ -z "$BOARD_NAME" ]; then
	echo "ERROR: -b option is required for system-image script." >&2
	exit 1
fi

TARGET_DIR="${TARGET_DIR:-${ALPINE_ROOTFS_DIR}}"
BINARIES_DIR="${BINARIES_DIR:-${TARGET_OUT}}"

if [ -z "${TARGET_DIR}" ] || [ ! -d "${TARGET_DIR}" ]; then
	echo "ERROR: TARGET_DIR missing or invalid." >&2
	exit 1
fi
if [ -z "${BINARIES_DIR}" ]; then
	echo "ERROR: BINARIES_DIR missing." >&2
	exit 1
fi

ALPINE_DIR="${ALPINE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"

mkdir -p "${BINARIES_DIR}"

# Stage system.erofs rootfs. Tar the rootfs excluding proc/sys/dev (which may
# still contain mount artifacts from assemble_rootfs), then extract to a clean
# staging directory so mkfs.erofs sees only real files.
echo "Building system.erofs for Alpine..."
EROF_STAGE=$(mktemp -d)
umount -f -R "${TARGET_DIR}/proc" 2>/dev/null || umount -lf "${TARGET_DIR}/proc" 2>/dev/null || true
umount -f -R "${TARGET_DIR}/sys" 2>/dev/null || umount -lf "${TARGET_DIR}/sys" 2>/dev/null || true
umount -f -R "${TARGET_DIR}/dev" 2>/dev/null || umount -lf "${TARGET_DIR}/dev" 2>/dev/null || true
(cd "${TARGET_DIR}" && tar -cf - --exclude='./proc/*' --exclude='./sys/*' --exclude='./dev/*' .) |
	(cd "${EROF_STAGE}" && tar -xf -)
mkdir -p "${EROF_STAGE}/mnt/sdcard"
ln -sf sdcard "${EROF_STAGE}/mnt/SDCARD"
mkfs.erofs -z lz4hc "${BINARIES_DIR}/system.erofs" "${EROF_STAGE}"
rm -rf "${EROF_STAGE}"

# Assemble the custom boot-stage initramfs.
echo "Assembling initramfs..."
INITRD_STAGE=$(mktemp -d)
mkdir -p "${INITRD_STAGE}/bin" "${INITRD_STAGE}/sbin" "${INITRD_STAGE}/lib" "${INITRD_STAGE}/usr" \
	"${INITRD_STAGE}/proc" "${INITRD_STAGE}/sys" "${INITRD_STAGE}/dev" \
	"${INITRD_STAGE}/tmp" "${INITRD_STAGE}/mnt/sdcard" "${INITRD_STAGE}/mnt/system"

ln -sf lib "${INITRD_STAGE}/lib64"
ln -sf ../lib "${INITRD_STAGE}/usr/lib"
ln -sf ../lib "${INITRD_STAGE}/usr/lib64"

# Alpine's standard busybox is dynamically linked against musl; the initramfs
# runs before any dynamic linker is available on /mnt/system. Use
# busybox-static instead (installs to /bin/busybox.static).
BUSYBOX_STATIC="${TARGET_DIR}/bin/busybox.static"
if [ ! -f "${BUSYBOX_STATIC}" ]; then
	echo "ERROR: busybox.static not found at ${BUSYBOX_STATIC} - add busybox-static to alpine/packages.txt" >&2
	exit 1
fi
cp -f "${BUSYBOX_STATIC}" "${INITRD_STAGE}/bin/busybox"
for app in sh mount mountpoint umount sleep reboot cp mkdir rm cat echo dd grep sync chroot date blkid; do
	ln -sf busybox "${INITRD_STAGE}/bin/${app}"
done
ln -sf ../bin/busybox "${INITRD_STAGE}/sbin/switch_root"

FSCK_FAT=""
for candidate in "${TARGET_DIR}/sbin/fsck.fat" "${TARGET_DIR}/usr/sbin/fsck.fat"; do
	if [ -x "${candidate}" ]; then
		FSCK_FAT="${candidate}"
		break
	fi
done
if [ -z "${FSCK_FAT}" ]; then
	echo "ERROR: fsck.fat not found - add dosfstools to alpine/packages.txt" >&2
	exit 1
fi
cp -a "${FSCK_FAT}" "${INITRD_STAGE}/sbin/fsck.fat"
cp -d "${TARGET_DIR}/lib/ld-"*.so* "${INITRD_STAGE}/lib/" 2>/dev/null || true
cp -d "${TARGET_DIR}/lib/libc."* "${INITRD_STAGE}/lib/" 2>/dev/null || true

# Optional: bootsplash reads the trait registry to size the framebuffer. We do
# not build it (Minime has no LICENSE file, so we do not vendor its source), so
# initramfs-init.sh's `if [ -x ... ]` guard simply skips it.
if [ -x "${TARGET_DIR}/usr/bin/bootsplash" ]; then
	mkdir -p "${INITRD_STAGE}/usr/bin"
	cp -a "${TARGET_DIR}/usr/bin/bootsplash" "${INITRD_STAGE}/usr/bin/bootsplash"
fi

if [ -d "${TARGET_DIR}/usr/share/minime/traits" ]; then
	mkdir -p "${INITRD_STAGE}/usr/share/nextui"
	cp -a "${TARGET_DIR}/usr/share/minime/traits" "${INITRD_STAGE}/usr/share/minime/"
fi

if [ -f "${ALPINE_DIR}/boards/common/initramfs-init.sh" ]; then
	cp -f "${ALPINE_DIR}/boards/common/initramfs-init.sh" "${INITRD_STAGE}/init"
	chmod +x "${INITRD_STAGE}/init"
fi

(cd "${INITRD_STAGE}" && find . | cpio -H newc -o >"${BINARIES_DIR}/initramfs.img")
rm -rf "${INITRD_STAGE}"

echo "Alpine system-image stage complete."
