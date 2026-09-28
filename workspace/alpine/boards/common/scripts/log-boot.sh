#!/bin/sh
# shellcheck shell=sh
# log-boot.sh: append a timestamped marker to the current boot's boot.log.
# Usage: log-boot.sh <TAG> <message...>
#
# Resolves the active per-boot log dir via .minime/logs/current (set by the
# initramfs).  Falls back to the FAT-root boot.log if no boot-id exists yet.
# Message format: "[TAG HH:MM:SS] message"

set -eu

LOGS_DIR="/mnt/sdcard/.minime/logs"
DEFAULT_LOG="/mnt/sdcard/boot.log"

[ $# -ge 1 ] || {
	echo "Usage: ${0##*/} <TAG> <message...>" >&2
	exit 1
}

tag="$1"
shift

log_file="${DEFAULT_LOG}"
boot_id=""
if [ -r "${LOGS_DIR}/current" ]; then
	boot_id="$(cat "${LOGS_DIR}/current" 2>/dev/null || true)"
fi
if [ -n "${boot_id}" ] && [ -d "${LOGS_DIR}/${boot_id}" ]; then
	log_file="${LOGS_DIR}/${boot_id}/boot.log"
fi

stamp="$(date -u +'%T' 2>/dev/null || date 2>/dev/null || true)"
{
	printf '[%s %s] %s\n' "${tag}" "${stamp}" "$*"
} >>"${log_file}" 2>/dev/null || true
sync 2>/dev/null || true
