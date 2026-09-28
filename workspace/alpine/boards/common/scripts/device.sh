#!/bin/sh
# shellcheck shell=sh
# device.sh: manage /mnt/sdcard/.minime/config/device.cfg
# Supports build-time configuration generation and runtime key/value manipulation.
# Subcommands:
#   init-cfg <target_cfg_file>
#   get <key> [cfg_file]
#   set <key> <value> [cfg_file]

set -eu

DEFAULT_CFG_PATH="/mnt/sdcard/.minime/config/device.cfg"

usage() {
	echo "Usage: ${0##*/} init-cfg <target_cfg_file>" >&2
	echo "       ${0##*/} get <key> [cfg_file]" >&2
	echo "       ${0##*/} set <key> <value> [cfg_file]" >&2
	exit 1
}

init_cfg() {
	target_cfg="$1"
	target_dir="$(dirname "${target_cfg}")"
	mkdir -p "${target_dir}"

	cat <<'EOF' >"${target_cfg}"
# minime Device Configuration
#
EOF

	if [ "${SOC_NAME:-}" = "rk3566" ]; then
		cat <<'EOF' >>"${target_cfg}"
# CPU undervolt (RK3566 only). Lowers CPU core voltage per OPP to reduce
# power and thermals.
# Allowed values: off, l1, l2, l3 (l3 is most aggressive).
# Default: l1 (conservative undervolt). Recovery: set undervolt=off.
undervolt=l1
EOF
	fi

	if [ "${AUTODETECT_SUPPORTED:-}" = "y" ]; then
		cat <<'EOF' >>"${target_cfg}"
# By default, this is set to 'auto' to automatically detect your device.
# If autodetection fails or you need to force a specific device/screen panel revision,
# uncomment and set 'device' to one of the built-in options listed below.
#
# Supported device options:
EOF
		default_device="auto"
	else
		cat <<'EOF' >>"${target_cfg}"
# Autodetection is not supported on this platform.
# You must set 'device' to one of the built-in options listed below
# matching your specific handheld device.
#
# Supported device options:
EOF
		default_device="${DEFAULT_DTB:-auto}"
	fi

	if [ -n "${BINARIES_DIR:-}" ] && [ -n "${DTB_PATTERN:-}" ]; then
		for dtb_file in "${BINARIES_DIR}"/${DTB_PATTERN}; do
			if [ -f "${dtb_file}" ]; then
				echo "# - $(basename "${dtb_file}")" >>"${target_cfg}"
			fi
		done
	fi
	cat <<EOF >>"${target_cfg}"
#
device=${default_device}
EOF
}

get_cfg() {
	key="$1"
	cfg_file="${2:-${DEFAULT_CFG_PATH}}"
	[ -f "${cfg_file}" ] || return 1
	grep -E "^[[:space:]]*${key}=" "${cfg_file}" | tail -n 1 | cut -d'=' -f2-
}

set_cfg() {
	key="$1"
	val="$2"
	cfg_file="${3:-${DEFAULT_CFG_PATH}}"
	[ -f "${cfg_file}" ] || {
		mkdir -p "$(dirname "${cfg_file}")"
		touch "${cfg_file}"
	}

	if grep -q -E "^[[:space:]]*${key}=" "${cfg_file}"; then
		sed -i "s|^[[:space:]]*${key}=.*|${key}=${val}|" "${cfg_file}"
	else
		echo "${key}=${val}" >>"${cfg_file}"
	fi
}

[ $# -ge 1 ] || usage

cmd="$1"
shift

case "${cmd}" in
init-cfg)
	[ $# -eq 1 ] || usage
	init_cfg "$1"
	;;
get)
	[ $# -ge 1 ] && [ $# -le 2 ] || usage
	get_cfg "$@"
	;;
set)
	[ $# -ge 2 ] && [ $# -le 3 ] || usage
	set_cfg "$@"
	;;
*)
	usage
	;;
esac
