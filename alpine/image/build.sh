#!/bin/sh
# shellcheck shell=sh
# NextUI image and update-package builder.
#
# Stitches the two halves of the system onto a FAT32 SD card image:
#   * the Alpine OS artifacts produced by alpine/scripts/build.sh
#     (Image, initramfs.img, system.erofs, *.dtb)
#   * the NextUI payload produced by the workspace/ makefile
#     (releases/<name>-base.zip and -extras.zip)
#
# Derived from Minime's packages/image/build.sh. Changes: no --target/--ui
# arguments (NextUI is the only UI and Alpine the only OS), the NextUI zips are
# unpacked directly instead of a per-UI tarball, and the rk3326 branch is gone.
#
# ON-DEVICE PATHS ARE STILL .minime/ -- DELIBERATELY.
# The staging root is .minime/, not .nextui/. That name is load-bearing:
# boards/common/boot.cmd hardcodes .minime/kernel, .minime/initramfs,
# .minime/devices/ and .minime/boot.log, and seventeen files under
# boards/common/overlay/ read /mnt/sdcard/.minime/traits (init.d/traits,
# init.d/gpudriver, init.d/logger, fstab, update.sh, mdns, ...). Renaming it is
# a mechanical rename, but it is a rename of the single most fragile part of
# the system -- the path the bootloader fatloads -- for cosmetics. It stays
# until it is done deliberately, on hardware, as its own change.
# Repo-internal paths (alpine/boards, NEXTUI_ROOT, alpine/packages.txt) are
# ours and are not Minime's.
#
# Usage:
#   ./build.sh --board <h700|rk3566> --input-dir <dir> --output-dir <dir>
#              [--payload-dir <dir>] [--nextui-release <name>]

set -eu

BOARD="" INPUT_DIR="" OUTPUT_DIR="" PAYLOAD_DIR="" NEXTUI_RELEASE=""

usage() {
	cat >&2 <<EOF
Usage: $0 --board <h700|rk3566> --input-dir <dir> --output-dir <dir>
          [--payload-dir <dir>] [--nextui-release <name>]

  --input-dir   dir holding Image, initramfs.img, system.erofs, *.dtb
                (normally alpine/out/<board>)
  --output-dir  where to write the .img.zst and .tar.zst
  --payload-dir dir holding the NextUI zips (default: <repo>/releases)
  --nextui-release
                release basename, e.g. NextUI-20260928. Matches make name.
EOF
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	--board)
		BOARD="$2"
		shift 2
		;;
	--input-dir)
		INPUT_DIR="$2"
		shift 2
		;;
	--output-dir)
		OUTPUT_DIR="$2"
		shift 2
		;;
	--payload-dir)
		PAYLOAD_DIR="$2"
		shift 2
		;;
	--nextui-release)
		NEXTUI_RELEASE="$2"
		shift 2
		;;
	-h | --help) usage ;;
	*)
		echo "Unknown arg: $1" >&2
		usage
		;;
	esac
done

if [ -z "$BOARD" ] || [ -z "$INPUT_DIR" ] || [ -z "$OUTPUT_DIR" ]; then
	echo "ERROR: --board, --input-dir and --output-dir are all required." >&2
	usage
fi

case "$BOARD" in
rk3566 | h700) ;;
*)
	echo "ERROR: unsupported board '$BOARD' (supported: h700, rk3566)" >&2
	exit 1
	;;
esac

ALPINE_DIR="${ALPINE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
NEXTUI_ROOT="${NEXTUI_ROOT:-$(cd "${ALPINE_DIR}/.." && pwd)}"
BOARD_DIR="${ALPINE_DIR}/boards/${BOARD}"
COMMON_DIR="${ALPINE_DIR}/boards/common"
BOOTLOADER_DIR="${ALPINE_DIR}/bootloader/${BOARD}/out"

[ -d "$BOARD_DIR" ] || {
	echo "ERROR: no board dir $BOARD_DIR" >&2
	exit 1
}
PAYLOAD_DIR="${PAYLOAD_DIR:-${NEXTUI_ROOT}/releases}"
mkdir -p "$OUTPUT_DIR"

# Resolve the NextUI release name if not given. Same computation the makefile
# uses for RELEASE_NAME, so the payload and the image agree.
if [ -z "$NEXTUI_RELEASE" ]; then
	NEXTUI_RELEASE="$(make -C "$NEXTUI_ROOT" name 2>/dev/null | tail -1)"
fi
[ -n "$NEXTUI_RELEASE" ] || {
	echo "ERROR: could not resolve NextUI release name" >&2
	exit 1
}

WORK_TMP="$(mktemp -d)"
trap 'rm -rf "${WORK_TMP}"' EXIT
STAGE_DIR="${WORK_TMP}/stage"
BINARIES_DIR="${WORK_TMP}/binaries"
ROOTPATH_TMP="${WORK_TMP}/rootpath"
mkdir -p "$OUTPUT_DIR" "$BINARIES_DIR" "$ROOTPATH_TMP" \
	"${STAGE_DIR}/.minime/devices" "${STAGE_DIR}/.minime/config/bluetooth"

# --- 1. Stage the OS payload ------------------------------------------------

for f in Image:kernel initramfs.img:initramfs system.erofs:system; do
	src="${f%%:*}"
	dst="${f##*:}"
	[ -f "${INPUT_DIR}/${src}" ] || {
		echo "ERROR: ${src} missing in ${INPUT_DIR}" >&2
		exit 1
	}
	cp -f "${INPUT_DIR}/${src}" "${STAGE_DIR}/.minime/${dst}"
done
cp -f "${INPUT_DIR}"/*.dtb "${STAGE_DIR}/.minime/devices/" 2>/dev/null || true

DEFAULT_DEVICE=""
if [ -f "${BOARD_DIR}/boot.env" ]; then
	DEFAULT_DEVICE="$(grep '^DEFAULT_DEVICE=' "${BOARD_DIR}/boot.env" | head -1 | cut -d= -f2- | tr -d '"' || true)"
fi
if [ -n "$DEFAULT_DEVICE" ] && [ -f "${STAGE_DIR}/.minime/devices/${DEFAULT_DEVICE}" ]; then
	cp -f "${STAGE_DIR}/.minime/devices/${DEFAULT_DEVICE}" "${STAGE_DIR}/.minime/dtb"
else
	first_dtb="$(find "${STAGE_DIR}/.minime/devices" -name '*.dtb' 2>/dev/null | sort | head -1 || true)"
	[ -n "$first_dtb" ] && cp -f "$first_dtb" "${STAGE_DIR}/.minime/dtb" ||
		echo "WARNING: no DTBs found in ${INPUT_DIR}; boot.cmd will fall back to its built-in default_device" >&2
fi

# --- 2. Stage the NextUI payload --------------------------------------------

base_zip="${PAYLOAD_DIR}/${NEXTUI_RELEASE}-base.zip"
extras_zip="${PAYLOAD_DIR}/${NEXTUI_RELEASE}-extras.zip"
if [ ! -f "$base_zip" ]; then
	echo "ERROR: NextUI base payload not found: $base_zip" >&2
	echo "       Build it first:  task payload" >&2
	exit 1
fi
log_stage() { printf '[image] %s\n' "$*" >&2; }
log_stage "unpacking ${base_zip}"
unzip -q -o "$base_zip" -d "$STAGE_DIR"
if [ -f "$extras_zip" ]; then
	log_stage "unpacking ${extras_zip}"
	unzip -q -o "$extras_zip" -d "$STAGE_DIR"
else
	log_stage "WARNING: no extras zip at ${extras_zip}; paks and tools will be missing"
fi

# The NextUI payload unpacks to BASE/, SYSTEM/ and EXTRAS/. Minime's image
# layout uses hidden dot-directories, so move them in and hide them.
mkdir -p "${STAGE_DIR}/.system"
if [ -d "${STAGE_DIR}/SYSTEM" ]; then
	mv "${STAGE_DIR}/SYSTEM/." "${STAGE_DIR}/.system/"
	rm -rf "${STAGE_DIR}/SYSTEM"
fi
if [ -d "${STAGE_DIR}/EXTRAS" ]; then
	rm -rf "${STAGE_DIR}/Tools"
	mv "${STAGE_DIR}/EXTRAS/Tools" "${STAGE_DIR}/Tools" 2>/dev/null || true
	rm -rf "${STAGE_DIR}/EXTRAS"
fi

NEXTUI_COMMIT="$(git -C "$NEXTUI_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
cat <<JSON >"${STAGE_DIR}/.minime/manifest.json"
{
  "board": "${BOARD}",
  "release": "${NEXTUI_RELEASE}",
  "nextui_commit": "${NEXTUI_COMMIT}",
  "nextui_version": "6.14.0",
  "os": "alpine",
  "timestamp": "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
}
JSON

# --- 3. Update package -------------------------------------------------------

PKG_NAME="nextui-alpine-${BOARD}-${NEXTUI_RELEASE}"
(cd "$STAGE_DIR" && tar -cf - . | zstd -q -9 >"${OUTPUT_DIR}/${PKG_NAME}.tar.zst")
cp -f "${OUTPUT_DIR}/${PKG_NAME}.tar.zst" "${OUTPUT_DIR}/nextui-alpine-${BOARD}.tar.zst"
log_stage "update package: ${OUTPUT_DIR}/${PKG_NAME}.tar.zst"

# --- 4. Boot script and device config ---------------------------------------

if [ -f "${COMMON_DIR}/boot.cmd" ] && [ -f "${BOARD_DIR}/boot.env" ]; then
	BOOTARGS=""
	EXTRA_ENV=""
	# shellcheck source=/dev/null
	. "${BOARD_DIR}/boot.env"
	sed -e "s|@BOOTARGS@|${BOOTARGS}|g" \
		-e "s|@DEFAULT_DEVICE@|${DEFAULT_DEVICE}|g" \
		-e "s|@EXTRA_ENV@|${EXTRA_ENV}|g" \
		"${COMMON_DIR}/boot.cmd" >"${WORK_TMP}/boot.cmd"
	mkimage -C none -A arm -T script -d "${WORK_TMP}/boot.cmd" "${STAGE_DIR}/boot.scr"
fi
if [ -x "${COMMON_DIR}/scripts/device.sh" ]; then
	"${COMMON_DIR}/scripts/device.sh" init-cfg "${STAGE_DIR}/.minime/config/device.cfg"
fi
touch "${STAGE_DIR}/.minime/config/first_boot_expand"
echo 1 >"${STAGE_DIR}/.minime/config/bluetooth/enabled"

# --- 5. Bootloader blobs -----------------------------------------------------

# The bootloader is not built here. The blobs are vendored under
# alpine/bootloader/<board>/out/ -- see alpine/bootloader/README.md for
# provenance and licensing -- so image assembly needs no u-boot toolchain.
stage_bootloader_file() {
	src="$1"
	dst="$2"
	if [ ! -f "${src}" ]; then
		echo "WARNING: ${src##*/} not found in ${BOOTLOADER_DIR}; image will not be bootable" >&2
		return 1
	fi
	if ! cp -f "${src}" "${dst}"; then
		# Distinct from "missing": a copy failure is a permissions or space
		# problem, and reporting it as a missing file sends you hunting for
		# the wrong thing.
		echo "ERROR: failed to copy ${src} to ${dst}" >&2
		return 1
	fi
}

if [ -d "$BOOTLOADER_DIR" ]; then
	if [ "${BOARD}" = "h700" ]; then
		stage_bootloader_file "${BOOTLOADER_DIR}/u-boot-sunxi-with-spl.bin" \
			"${BINARIES_DIR}/" || true
		# Staged, not flashed. boards/h700/genimage.cfg puts the LPDDR4 build
		# at offset 8K; this LPDDR3 build sits on the FAT partition as a
		# fallback that initramfs-init.sh only swaps in after measuring
		# vdd-dram at exactly 1200mV. Inert on DDR4 hardware such as the
		# RG35XX SP v1, where that regulator reads 1100mV.
		stage_bootloader_file "${BOOTLOADER_DIR}/u-boot-sunxi-with-spl-ddr3.bin" \
			"${STAGE_DIR}/.minime/u-boot-ddr3.bin" || true
	else
		for f in idbloader.img u-boot.itb; do
			stage_bootloader_file "${BOOTLOADER_DIR}/${f}" "${BINARIES_DIR}/" || true
		done
	fi
else
	echo "ERROR: ${BOOTLOADER_DIR} does not exist; image will not be bootable" >&2
fi

# --- 6. Format FAT32 userdata and run genimage ------------------------------

STAGE_MB="$(du -sm "$STAGE_DIR" | cut -f1)"
VFAT_MB=$((STAGE_MB + 256))
[ "$VFAT_MB" -lt 1040 ] && VFAT_MB=1040
dd if=/dev/zero of="${BINARIES_DIR}/userdata.vfat" bs=1M count="${VFAT_MB}" status=none
mkdosfs -F 32 -s 32 -n nextui "${BINARIES_DIR}/userdata.vfat"
[ -f "${STAGE_DIR}/boot.scr" ] && MTOOLS_SKIP_CHECK=1 mcopy -i "${BINARIES_DIR}/userdata.vfat" "${STAGE_DIR}/boot.scr" ::boot.scr
for item in .minime .system Tools; do
	if [ -e "${STAGE_DIR}/${item}" ]; then
		if MTOOLS_SKIP_CHECK=1 mcopy -i "${BINARIES_DIR}/userdata.vfat" -s "${STAGE_DIR}/${item}" ::; then
			MTOOLS_SKIP_CHECK=1 mattrib -i "${BINARIES_DIR}/userdata.vfat" +h "::${item}" || true
		fi
	fi
done
# Anything else in the stage (BASE/, BOOT/ etc. from the NextUI payload).
for item in "${STAGE_DIR}"/*; do
	[ -e "$item" ] || continue
	b="$(basename "$item")"
	[ "$b" = "boot.scr" ] && continue
	case "$b" in
	.minime | .system | Tools) continue ;;
	esac
	MTOOLS_SKIP_CHECK=1 mcopy -i "${BINARIES_DIR}/userdata.vfat" -s "$item" ::
done

GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
[ -f "$GENIMAGE_CFG" ] || GENIMAGE_CFG="${COMMON_DIR}/genimage.cfg"
cp -f "$GENIMAGE_CFG" "${ROOTPATH_TMP}/genimage.cfg"
sed -i "s/__IMAGE_NAME__/${PKG_NAME}.img/g" "${ROOTPATH_TMP}/genimage.cfg"

genimage --rootpath "${ROOTPATH_TMP}" --tmppath "${WORK_TMP}/genimage.tmp" \
	--inputpath "${BINARIES_DIR}" --outputpath "${OUTPUT_DIR}" \
	--config "${ROOTPATH_TMP}/genimage.cfg"
zstd -q -9 -f "${OUTPUT_DIR}/${PKG_NAME}.img" -o "${OUTPUT_DIR}/${PKG_NAME}.img.zst"
rm -f "${OUTPUT_DIR}/${PKG_NAME}.img"
cp -f "${OUTPUT_DIR}/${PKG_NAME}.img.zst" "${OUTPUT_DIR}/nextui-alpine-${BOARD}.img.zst"
log_stage "image: ${OUTPUT_DIR}/${PKG_NAME}.img.zst"
