#!/bin/sh
# shellcheck shell=sh
# update: self-update Minime from the GitHub "testing" release.
#
# Usage: update [target] [ui]
#        update <minui|allium|alpine|buildroot>
#        update --target <alpine|buildroot> --ui <minui|allium>
#
# Detects board (h700/rk3326/rk3566) and target (alpine/buildroot) on-device,
# downloads the matching OTA archive directly with curl, compares it against
# the installed manifest, and installs it (clean-replaces the active UI
# payload — .system for MinUI, .ui/.allium/RetroArch/apps for Allium —
# and overlays .minime).  User data (ROMs content, Saves/, Bios/,
# .userdata/) is never touched.
#
# The script detaches from the invoking shell by default (telnet-safe) and
# logs to the SD card, so it survives the session dropping.  It reboots the
# device when the update has been applied.

set -eu

# --- Configuration ---------------------------------------------------------

REPO="jheronimus/minime"
RELEASE="testing"
SDCARD="/mnt/sdcard"
UPDATE_DIR="${SDCARD}/.minime/update"
LOG_FILE="${UPDATE_DIR}/update.log"
PID_FILE="${UPDATE_DIR}/update.pid"
INSTALLED_MANIFEST="${SDCARD}/.minime/manifest.json"
UI_ENV_FILE="${SDCARD}/.minime/ui.env"
TRAITS_FILE="${SDCARD}/.minime/traits"

# --- Helpers --------------------------------------------------------------

usage() {
	echo "Usage: ${0##*/} [alpine|buildroot] [minui|allium|muos|blast16]" >&2
	exit 1
}

log() {
	echo "[update] $*"
}

die() {
	log "ERROR: $*" >&2
	exit 1
}

# Detach from the invoking shell (telnet-safe): re-run ourselves under setsid
# with output redirected to the SD-card log, then return immediately.
detach() {
	mkdir -p "${UPDATE_DIR}"
	if [ -f "${PID_FILE}" ] && kill -0 "$(cat "${PID_FILE}" 2>/dev/null)" 2>/dev/null; then
		echo "[update] ERROR: already running (pid $(cat "${PID_FILE}" 2>/dev/null))" >&2
		exit 1
	fi
	echo "[update] starting in background; log: ${LOG_FILE}"
	UPDATE_DETACHED=1 setsid /bin/sh "$0" "$@" </dev/null >>"${LOG_FILE}" 2>&1 &
	echo $! >"${PID_FILE}"
	exit 0
}

# Detect the board (h700/rk3326/rk3566) from the kernel compatible string,
# falling back to the staged dtb filename.
detect_board() {
	compat="$(tr '\0' '\n' </proc/device-tree/compatible 2>/dev/null || true)"
	case "${compat}" in
	*"sun50i-h700"*) echo "h700" ;;
	*"rk3326"*) echo "rk3326" ;;
	*"rk3566"* | *"rk3568"*) echo "rk3566" ;;
	*)
		# Fallback: /mnt/sdcard/.minime/dtb, e.g. sun50i-h700-*.dtb
		dtb="$(basename "${SDCARD}/.minime/dtb" 2>/dev/null || true)"
		case "${dtb}" in
		"sun50i-h700-"*) echo "h700" ;;
		"rk3326-"*) echo "rk3326" ;;
		"rk3566-"* | "rk3568-"*) echo "rk3566" ;;
		*) die "cannot detect board (compatible='${compat}' dtb='${dtb}')" ;;
		esac
		;;
	esac
}

# Detect the target (alpine/buildroot) from /etc/os-release.
detect_target() {
	if grep -q '^ID=alpine' /etc/os-release 2>/dev/null; then
		echo "alpine"
	elif grep -qi '^ID=buildroot' /etc/os-release 2>/dev/null; then
		echo "buildroot"
	else
		# Fallback: gpu_driver trait (panfrost=alpine, mali_kbase=buildroot)
		if grep -q '^gpu_driver=mali_kbase' "${TRAITS_FILE}" 2>/dev/null; then
			echo "buildroot"
		else
			echo "alpine"
		fi
	fi
}

# Normalized (lowercase) UI name of the currently installed payload.
installed_ui() {
	[ -f "${UI_ENV_FILE}" ] || return 0
	grep '^UI_NAME=' "${UI_ENV_FILE}" 2>/dev/null | head -n1 |
		cut -d= -f2- | tr -d '"' | tr 'A-Z' 'a-z'
}

# One-time forward migration of Roms folders renamed by the Q6 unification to
# the single canonical MinUI scheme. For each legacy name whose canonical
# counterpart now exists (the new preloads stage them), the legacy folder is a
# duplicate and is dropped; if the canonical target is absent/empty the legacy
# content is moved instead. Idempotent and safe — only ever touches the four
# folders whose names changed.
migrate_roms() {
	roms="${SDCARD}/Roms"
	[ -d "${roms}" ] || return 0
	migrate_one "Game Gear (GG)" "Sega Game Gear (GG)"
	migrate_one "Master System (SMS)" "Sega Master System (SMS)"
	migrate_one "PC Engine (PCE)" "TurboGrafx-16 (PCE)"
	migrate_one "Super Nintendo (SFC)" "Super Nintendo Entertainment System (SFC)"
}

migrate_one() {
	legacy="$1"
	canon="$2"
	[ -d "${roms}/${legacy}" ] || return 0
	if [ -d "${roms}/${canon}" ] && [ -n "$(ls -A "${roms}/${canon}" 2>/dev/null)" ]; then
		log "dropping duplicate legacy Roms/${legacy} (canonical Roms/${canon} exists)"
		rm -rf "${roms:?}/${legacy:?}"
	else
		log "migrating Roms/${legacy} -> ${canon}"
		mkdir -p "${roms}"
		mv "${roms}/${legacy}" "${roms}/${canon}"
	fi
}

# --- Main -----------------------------------------------------------------

for arg in "$@"; do
	case "$arg" in
	-h | --help) usage ;;
	esac
done

if [ "${UPDATE_DETACHED:-0}" != "1" ]; then
	detach "$@"
fi

TARGET=""
UI=""

while [ $# -gt 0 ]; do
	case "$1" in
	--target)
		[ $# -ge 2 ] || usage
		TARGET="$2"
		shift 2
		;;
	--ui)
		[ $# -ge 2 ] || usage
		UI="$2"
		shift 2
		;;
	*)
		arg="$(echo "$1" | tr 'A-Z' 'a-z')"
		case "${arg}" in
		alpine | buildroot)
			TARGET="${arg}"
			;;
		minui | allium | muos | blast16)
			UI="${arg}"
			;;
		arc)
			UI="blast16"
			;;
		*)
			die "unsupported argument '$1' (expected alpine, buildroot, minui, allium, muos, or blast16)"
			;;
		esac
		shift
		;;
	esac
done

echo $$ >"${PID_FILE}"
trap 'rm -f "${PID_FILE}"' EXIT

BOARD="$(detect_board)"
FROM_TARGET="$(detect_target)"
FROM_UI="$(installed_ui)"

[ -n "${TARGET}" ] || TARGET="${FROM_TARGET}"
[ -n "${UI}" ] || UI="${FROM_UI:-minui}"

case "${TARGET}" in
alpine | buildroot) ;;
*) die "unsupported target '${TARGET}' (expected alpine or buildroot)" ;;
esac

case "${UI}" in
minui | allium | muos | blast16) ;;
*) die "unsupported UI '${UI}' (expected minui, allium, muos, or blast16)" ;;
esac

log "board=${BOARD} target=${TARGET} (installed: ${FROM_TARGET}) ui=${UI} (installed: ${FROM_UI:-unknown})"

[ -x /usr/bin/curl ] || die "curl is not available on this image"
[ -d "${SDCARD}" ] || die "no SD card at ${SDCARD}"

ARCHIVE="${UPDATE_DIR}/minime-${TARGET}-${BOARD}-${UI}.tar.zst"
URL="https://github.com/${REPO}/releases/download/${RELEASE}/minime-${TARGET}-${BOARD}-${UI}.tar.zst"

log "Checking ${URL}"
mkdir -p "${UPDATE_DIR}"

# Download. curl with -L follows the release redirect; --fail surfaces HTTP errors.
if ! curl -L --fail --show-error -o "${ARCHIVE}" "${URL}"; then
	rm -f "${ARCHIVE}"
	die "download failed for ${URL}"
fi

SIZE="$(wc -c <"${ARCHIVE}" 2>/dev/null || echo 0)"
log "downloaded ${SIZE} bytes"

# Compare the archive's manifest against the installed one.
REMOTE_MANIFEST="$(unzstd -c "${ARCHIVE}" 2>/dev/null | tar -xOf - ./.minime/manifest.json 2>/dev/null || true)"
if [ -n "${REMOTE_MANIFEST}" ] && [ -f "${INSTALLED_MANIFEST}" ]; then
	rem_target="$(echo "${REMOTE_MANIFEST}" | sed -n 's/.*"target": *"\([^"]*\)".*/\1/p' | head -n1)"
	rem_board="$(echo "${REMOTE_MANIFEST}" | sed -n 's/.*"board": *"\([^"]*\)".*/\1/p' | head -n1)"
	rem_ui="$(echo "${REMOTE_MANIFEST}" | sed -n 's/.*"ui": *"\([^"]*\)".*/\1/p' | head -n1)"
	rem_min="$(echo "${REMOTE_MANIFEST}" | sed -n 's/.*"minime_commit": *"\([^"]*\)".*/\1/p' | head -n1)"
	rem_ui_commit="$(echo "${REMOTE_MANIFEST}" | sed -n 's/.*"ui_commit": *"\([^"]*\)".*/\1/p' | head -n1)"

	loc_target="$(sed -n 's/.*"target": *"\([^"]*\)".*/\1/p' "${INSTALLED_MANIFEST}" | head -n1)"
	loc_board="$(sed -n 's/.*"board": *"\([^"]*\)".*/\1/p' "${INSTALLED_MANIFEST}" | head -n1)"
	loc_ui="$(sed -n 's/.*"ui": *"\([^"]*\)".*/\1/p' "${INSTALLED_MANIFEST}" | head -n1)"
	loc_min="$(sed -n 's/.*"minime_commit": *"\([^"]*\)".*/\1/p' "${INSTALLED_MANIFEST}" | head -n1)"
	loc_ui_commit="$(sed -n 's/.*"ui_commit": *"\([^"]*\)".*/\1/p' "${INSTALLED_MANIFEST}" | head -n1)"

	if [ "${rem_target}" = "${loc_target}" ] &&
		[ "${rem_board}" = "${loc_board}" ] &&
		[ "${rem_ui}" = "${loc_ui}" ] &&
		[ "${rem_min}" = "${loc_min}" ] &&
		[ "${rem_ui_commit}" = "${loc_ui_commit}" ] &&
		[ -n "${rem_min}" ] && [ "${rem_min}" != "unknown" ]; then
		log "already up to date (${rem_target}/${rem_ui} ${rem_min}/${rem_ui_commit})"
		rm -f "${ARCHIVE}"
		rmdir "${UPDATE_DIR}" 2>/dev/null || true
		exit 0
	fi
fi

log "installing ${SIZE} bytes (${rem_target:-?}/${rem_ui:-?} ${rem_min:-?}/${rem_ui_commit:-?})..."

# Stop the UI so it is not running from files we are about to replace.
/etc/init.d/ui stop >/dev/null 2>&1 || true
killall -9 minui.elf minarch.elf keymon.elf alliumd muxfrontend 2>/dev/null || true
sleep 1

# Apply: UI payload clean-replaced, .minime overlaid (state kept).
# MinUI lives under .system/; Allium under .ui/ + .allium/ + RetroArch/ +
# apps/ + Roms/ + Saves/ + BIOS/; muOS lives under .muos/. Remove the old
# UI's top-level dirs so a UI switch does not leave stale binaries.
case "${FROM_UI:-${UI}}" in
minui) rm -rf "${SDCARD}/.system" ;;
allium)
	rm -rf "${SDCARD}/.ui" "${SDCARD}/.allium" \
		"${SDCARD}/RetroArch" "${SDCARD}/apps"
	;;
muos)
	rm -rf "${SDCARD}/.muos"
	;;
blast16)
	rm -rf "${SDCARD}/.blast"
	;;
esac
unzstd -c "${ARCHIVE}" | tar -xf - -C "${SDCARD}"

# Verify the payload landed before removing the archive.
case "${UI}" in
minui)
	[ -f "${SDCARD}/.system/version.txt" ] ||
		die "install incomplete: .system/version.txt missing; leaving archive at ${ARCHIVE}"
	;;
allium)
	[ -x "${SDCARD}/.ui/bin/alliumd" ] ||
		die "install incomplete: .ui/bin/alliumd missing; leaving archive at ${ARCHIVE}"
	;;
muos)
	[ -x "${SDCARD}/.muos/bin/muxfrontend" ] ||
		die "install incomplete: .muos/bin/muxfrontend missing; leaving archive at ${ARCHIVE}"
	;;
blast16)
	[ -x "${SDCARD}/.blast/blast16" ] ||
		die "install incomplete: .blast/blast16 missing; leaving archive at ${ARCHIVE}"
	;;
esac

# Migrate any pre-Q6 legacy Roms folder names to the canonical scheme
# (safe: drops duplicates, moves when the canonical target is missing).
migrate_roms

# Clean up the archive before rebooting.
rm -f "${ARCHIVE}"

log "update installed; rebooting"
sync
reboot
exit 0
