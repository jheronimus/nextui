#!/bin/sh
# shellcheck shell=sh
# NextUI Alpine post-build script: overlay the OpenRC services, system config,
# udev rules and board trait payload into an assembled rootfs.
#
# Derived from Minime's packages/components/alpine/scripts/post-build.sh.
# Changes: paths repointed at alpine/boards/, traits installed under
# /usr/share/minime/ instead of /usr/share/minime/, and the post-build
# `echo "gpu_driver=panfrost" >>` removed. Upstream appends that line to flip
# between Alpine and Buildroot; we are Alpine-only, so platform.ini declares it
# exactly once and appending here would emit it twice and rely on last-wins
# merging.

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
	echo "ERROR: -b option is required for post-build script." >&2
	exit 1
fi

TARGET_DIR="${TARGET_DIR:-${ALPINE_ROOTFS_DIR}}"
if [ -z "${TARGET_DIR}" ] || [ ! -d "${TARGET_DIR}" ]; then
	echo "ERROR: TARGET_DIR or ALPINE_ROOTFS_DIR is not set or missing." >&2
	exit 1
fi

ALPINE_DIR="${ALPINE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
BOARDS_DIR="${ALPINE_DIR}/boards"
BOARD_DIR="${BOARDS_DIR}/${BOARD_NAME}"
COMMON_DIR="${BOARDS_DIR}/common"

if [ ! -d "${BOARD_DIR}" ]; then
	echo "ERROR: Board directory ${BOARD_DIR} missing!" >&2
	exit 1
fi

# 1. Install the board's immutable trait payload.
# The trait registry belongs to the NextUI platform port
# (workspace/alpine/boards), not to the foundation board dir it is installed from.
TRAITS_DIR="${TRAITS_DIR:-$(cd "${ALPINE_DIR}/../workspace/alpine/boards/${BOARD}/traits" && pwd)}"
if [ -d "${TRAITS_DIR}" ]; then
	rm -rf "${TARGET_DIR}/usr/share/minime/traits"
	mkdir -p "${TARGET_DIR}/usr/share/minime/traits"
	cp -a "${TRAITS_DIR}/." "${TARGET_DIR}/usr/share/minime/traits/"
fi

# 2. Install the shared overlay (OpenRC services, system config, udev rules).
if [ -d "${COMMON_DIR}/overlay" ]; then
	cp -a "${COMMON_DIR}/overlay/." "${TARGET_DIR}/"
fi

# Alpine ships this hardening setting, but our kernel does not expose the
# corresponding sysctl. Keep the supported Alpine settings and omit only the
# unsupported write so boot does not report a false sysctl error.
if [ -f "${TARGET_DIR}/usr/lib/sysctl.d/00-alpine.conf" ]; then
	sed -i '/^[[:space:]]*kernel\.unprivileged_bpf_disabled[[:space:]]*=/d' \
		"${TARGET_DIR}/usr/lib/sysctl.d/00-alpine.conf"
fi

# 3. Install board-specific overlay if present.
if [ -d "${BOARD_DIR}/overlay" ]; then
	cp -a "${BOARD_DIR}/overlay/." "${TARGET_DIR}/"
fi

# 4. Install shared utility scripts.
mkdir -p "${TARGET_DIR}/usr/share/minime/scripts"
for s in device.sh log-boot.sh collect-diagnostics.sh audio.sh; do
	if [ -f "${COMMON_DIR}/scripts/${s}" ]; then
		install -m 0755 "${COMMON_DIR}/scripts/${s}" "${TARGET_DIR}/usr/share/minime/scripts/"
	fi
done

# 5. Point /etc/resolv.conf at /run/resolv.conf (tmpfs, rw).
ln -sf /run/resolv.conf "${TARGET_DIR}/etc/resolv.conf"

# 6. Marker used by initramfs-init.sh to advance system time on cold boot if the
#    hardware RTC is in the past (prevents OpenRC clock skew warnings).
touch "${TARGET_DIR}/.build_time"

echo "Alpine post-build stage complete."
