#!/bin/sh
# shellcheck shell=sh
# collect-diagnostics.sh: bundle device logs and config into a timestamped
# tarball at the FAT root for off-device debugging.
# Produces: /mnt/sdcard/minime-diagnostics-YYYYmmdd.tar.gz
#
# Contents: all per-boot logs (.minime/logs/), live dmesg, wifi diagnostics,
# device traits/config, ui.env, and UI build identity (.system/version.txt).

set -eu

ROOT="/mnt/sdcard"
STAGE="$(mktemp -d /tmp/diag.XXXXXX 2>/dev/null || echo /tmp/diag)"
mkdir -p "${STAGE}/logs" "${STAGE}/minime" "${STAGE}/system"
trap 'rm -rf "${STAGE}" 2>/dev/null || true' EXIT HUP INT TERM

copy_logs() {
	# Per-boot log dirs from the persistent logger.
	if [ -d "${ROOT}/.minime/logs" ]; then
		cp -a "${ROOT}/.minime/logs/." "${STAGE}/logs/" 2>/dev/null || true
	fi
	# Legacy root-level logs.
	cp -f "${ROOT}/boot.log" "${STAGE}/logs/" 2>/dev/null || true
	cp -f "${ROOT}/wifi.diagnostics" "${STAGE}/logs/" 2>/dev/null || true
	cp -f "${ROOT}/ui.log" "${STAGE}/logs/" 2>/dev/null || true
	# Current kernel ring buffer (live).
	dmesg >"${STAGE}/logs/dmesg.current" 2>/dev/null || true
}

copy_config() {
	cp -f "${ROOT}/.minime/traits" "${STAGE}/minime/" 2>/dev/null || true
	cp -f "${ROOT}/.minime/ui.env" "${STAGE}/minime/" 2>/dev/null || true
	cp -f "${ROOT}/.minime/manifest.json" "${STAGE}/minime/" 2>/dev/null || true
	if [ -d "${ROOT}/.minime/config" ]; then
		cp -a "${ROOT}/.minime/config/." "${STAGE}/minime/config/" 2>/dev/null || true
	fi
	cp -f "${ROOT}/.system/version.txt" "${STAGE}/system/" 2>/dev/null || true
	cp -f "${ROOT}/.system/commits.txt" "${STAGE}/system/" 2>/dev/null || true
}

copy_logs
copy_config

STAMP="$(date +%Y%m%d 2>/dev/null || date 2>/dev/null || echo unknown)"
OUT="${ROOT}/minime-diagnostics-${STAMP}.tar.gz"
rm -f "${OUT}"
tar -czf "${OUT}" -C "${STAGE}" . 2>/dev/null || {
	echo "collect-diagnostics: tar failed" >&2
	exit 1
}
sync
echo "${OUT}"
