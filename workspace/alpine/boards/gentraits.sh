#!/bin/sh
# gentraits.sh — device registry generator and validator.
#
# Derived from Minime's packages/image/gentraits.sh
# (https://github.com/jheronimus/minime), reduced to the two SoCs this fork
# supports. Upstream is MIT/GPL; see LICENSE.
#
# The device registry (alpine/boards/<board>/traits/) is the single source of
# truth for which devices we support. It mirrors the mainline DTS
# cascade: each trait file's `parent=` mirrors the DTS `#include`, so a derived
# device (e.g. rg35xx-sp-v2 = v1 body + a different panel) inherits its core
# traits instead of duplicating them.
#
# Modes:
#   check    <board>   validate the registry
#   overlays <board> <outdir>   emit overlay DTS files for derived devices
#   dtbs     <board>   print the shipped DTB paths (one per line) that the
#                      kernel builds must ship
#
# Differences from upstream, and why:
#   * RK3326 dropped. We ship H700 (RG35XX SP) and RK3566 (RG ARC-D) only, so
#     board_info/soc_compatible/panel_node and the rk3326 branches in
#     emit_overlay are gone.
#   * `makefile` mode dropped. It only ever produced output for RK3326; H700
#     and RK3566 build through the kernel's %.dtb: %.dts pattern rule.
#   * `check` no longer cross-references Buildroot's
#     BR2_LINUX_KERNEL_INTREE_DTS_NAME. There is no Buildroot in this fork, so
#     that half of the check was a no-op guard against a tree that cannot
#     exist here.
#
# Self-contained POSIX sh. No project-internal dependencies beyond the
# registry files themselves.
#
# The kernel patch series `input-name-devices-from-dt-node` is the load-bearing
# contract that makes trait device names match the compiled DTB. Without it the
# 21 key_* traits and every input_*_device_name below are fiction.

set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
BOARDS_ROOT="$ROOT"

usage() {
	echo "usage: $0 check <board> | overlays <board> <outdir> | dtbs <board>" >&2
	exit 2
}

# --- key/value helpers -----------------------------------------------------

# First value of KEY anywhere in FILE (init.d/traits reads the same way).
read_key() {
	key="$1"
	file="$2"
	sed -n "s/^${key}=//p" "$file" | head -n 1
}

# --- board metadata ---------------------------------------------------------

board_dir() { echo "$BOARDS_ROOT/$1"; }

# Board -> DTB directory + DTS filename prefix.
board_info() {
	case "$1" in
	h700) echo "allwinner sun50i-h700-anbernic-" ;;
	rk3566) echo "rockchip rk3566-anbernic-" ;;
	*)
		echo "unsupported board: $1 (supported: h700, rk3566)" >&2
		exit 1
		;;
	esac
}

soc_compatible() {
	case "$1" in
	h700) echo "allwinner,sun50i-h700" ;;
	rk3566) echo "rockchip,rk3566" ;;
	esac
}

# Default DTB path (relative, no extension) for a device trait file.
# Overridable per device with `[dts] dtb=`; `dtb=none` excludes the device.
device_dtb() {
	board="$1"
	file="$2"
	# shellcheck disable=SC2046
	set -- $(board_info "$board")
	dir="$1"
	prefix="$2"

	dtb="$(read_key dtb "$file")"
	if [ -n "$dtb" ]; then
		echo "$dtb"
		return
	fi
	echo "${dir}/$(read_key device_id "$file" | sed "s|^|${prefix}|")"
}

# --- registry helpers -------------------------------------------------------

# True if the device is a generated overlay (has a [dts] base).
is_generated() {
	[ -n "$(read_key base "$1")" ]
}

# Resolve the trait file of a device's parent (empty if none).
parent_file() {
	file="$1"
	parent="$(read_key parent "$file")"
	[ -n "$parent" ] || return 0
	[ -f "${file%/*}/${parent}.ini" ] || {
		echo "$file: unknown parent '$parent'" >&2
		return 1
	}
	echo "${file%/*}/${parent}.ini"
}

# --- check mode -------------------------------------------------------------

PLATFORM_KEYS="
screen_backlight_path screen_backlight_max screen_blank_path
cpu_governor_path cpu_clock_path cpu_thermal_path
gpu_device audio_card audio_mixer
input_gamepad_device_name input_power_device_name input_volume_device_name
input_lid_device_name input_rumble_device_name
power_battery_sysfs usb_otg usb_host_ports usb_device_mode usb_controller_mode
storage_sd_node
"

DEVICE_KEYS="
model compatible device_id device_model
screen_width screen_height screen_rotation screen_aspect screen_refresh_rate
gpu_hdmi_connector input_touch wifi_interface bluetooth_interface
input_axis_lx input_axis_ly input_axis_rx input_axis_ry
input_axis_min input_axis_center input_axis_max input_stick_device_name
"

check_board() {
	board="$1"
	dir="$(board_dir "$board")"
	platform="$dir/traits/platform.ini"
	fail=0

	need() {
		[ -f "$2" ] || {
			echo "missing file: $2" >&2
			return 1
		}
		grep -Eq "^${1}=..*" "$2" || {
			echo "$2: missing $1" >&2
			return 1
		}
	}

	[ -f "$platform" ] || {
		echo "missing platform traits: $platform" >&2
		return 1
	}
	for key in $PLATFORM_KEYS; do
		need "$key" "$platform" || fail=1
	done

	seen=""
	for file in "$dir"/traits/devices/*.ini; do
		[ -f "$file" ] || continue
		base="$(basename "$file" .ini)"
		parent="$(read_key parent "$file")"

		# Required keys: identity for everyone, full schema for cores.
		for key in model compatible device_id device_model; do
			need "$key" "$file" || fail=1
		done
		if [ -n "$parent" ]; then
			parent_file "$file" >/dev/null || fail=1
			[ "$parent" != "$base" ] || {
				echo "$file: parent cannot be itself" >&2
				fail=1
			}
		else
			for key in $DEVICE_KEYS; do
				need "$key" "$file" || fail=1
			done
		fi

		# A generated overlay must name its base include.
		if is_generated "$file"; then
			[ -n "$(read_key base "$file")" ] || {
				echo "$file: [dts] base is empty" >&2
				fail=1
			}
		fi

		# Explicit dtb must start with dir/ prefix (unless none)
		explicit_dtb="$(read_key dtb "$file")"
		if [ -n "$explicit_dtb" ] && [ "$explicit_dtb" != "none" ]; then
			# shellcheck disable=SC2046
			set -- $(board_info "$board")
			b_dir="$1"
			case "$explicit_dtb" in
			"${b_dir}/"*) ;;
			*)
				echo "$file: explicit dtb '$explicit_dtb' must start with '${b_dir}/'" >&2
				fail=1
				;;
			esac
		fi

		# Duplicate [match] detection.
		match="$(read_key model "$file")|$(read_key compatible "$file")"
		case " $seen " in
		*" $match "*)
			echo "$file: duplicate match ($match)" >&2
			fail=1
			;;
		esac
		seen="$seen $match"

		# Obsolete or empty trait values.
		if grep -Eq '^(has_|button_layout=)|=$' "$file" "$platform"; then
			echo "$file: obsolete or empty trait" >&2
			fail=1
		fi
	done

	# Every parent-less (core) device must resolve to a shipped DTB: the
	# registry drives the kernel builds, so a core device with no DTB is an
	# unreachable device.
	for file in "$dir"/traits/devices/*.ini; do
		[ -f "$file" ] || continue
		[ -n "$(read_key parent "$file")" ] && continue
		dtb="$(device_dtb "$board" "$file")"
		[ -n "$dtb" ] || {
			echo "$file: core device has no shipped DTB" >&2
			fail=1
		}
	done

	[ "$fail" -eq 0 ] || return 1
	echo "traits check passed ($board)"
}

# Registry-derived DTB paths for a board (sortable, deduplicated).
dtbs_for_board() {
	board="$1"
	dir="$(board_dir "$board")"
	for file in "$dir"/traits/devices/*.ini; do
		[ -f "$file" ] || continue
		[ "$(read_key dtb "$file")" = "none" ] && continue
		device_dtb "$board" "$file"
	done | sort -u
}

# --- overlays mode ----------------------------------------------------------

panel_node() {
	case "$1" in
	h700) echo "panel" ;;
	rk3566) echo "internal_display" ;;
	esac
}

emit_overlay() {
	board="$1"
	file="$2"
	outdir="$3"
	base="$(read_key base "$file")"
	panel="$(read_key panel "$file")"
	compat_parent="$(read_key compat_parent "$file")"
	dtb="$(device_dtb "$board" "$file")"
	[ "$dtb" != "none" ] || return 0

	name="${dtb##*/}"
	model="$(read_key model "$file")"
	compat="$(read_key compatible "$file")"
	soc="$(soc_compatible "$board")"
	parent_model=""
	if pfile="$(parent_file "$file")" && [ -n "$pfile" ]; then
		parent_model="$(read_key model "$pfile")"
		parent_compat="$(read_key compatible "$pfile")"
	fi

	out="$outdir/$name.dts"
	mkdir -p "$outdir"

	{
		echo "#include \"$base\""
		echo ""
		echo "/ {"
		if [ "$model" != "$parent_model" ]; then
			echo "	model = \"$model\";"
		fi
		if [ "$compat_parent" = "1" ]; then
			printf '\tcompatible = "%s", "%s", "%s";\n' "$compat" "$parent_compat" "$soc"
		else
			printf '\tcompatible = "%s", "%s";\n' "$compat" "$soc"
		fi
		echo "};"
		echo ""
		node="$(panel_node "$board")"
		if [ -n "$panel" ]; then
			echo "&$node {"
			# H700 panels are driven by the generic panel-mipi-dpi-spi driver.
			if [ "$board" = "h700" ]; then
				printf '\tcompatible = "%s", "panel-mipi-dpi-spi";\n' "$(echo "$panel" | sed 's/ /", "/g')"
			else
				printf '\tcompatible = "%s";\n' "$(echo "$panel" | sed 's/ /", "/g')"
			fi
			echo "};"
		fi
	} >"$out"

	tmp="$out.tmp"
	{
		echo "// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)"
		echo "/*"
		echo " * Generated by gentraits.sh from the NextUI device registry."
		echo " * Derived from Minime (https://github.com/jheronimus/minime)."
		echo " */"
		echo ""
		cat "$out"
	} >"$tmp"
	mv "$tmp" "$out"
	echo "$out"
}

# --- main -------------------------------------------------------------------

[ "$#" -ge 2 ] || usage
mode="$1"
board="$2"
case "$mode" in
check)
	check_board "$board"
	;;
dtbs)
	dtbs_for_board "$board"
	;;
overlays)
	[ "$#" -eq 3 ] || usage
	outdir="$3"
	for file in "$(board_dir "$board")"/traits/devices/*.ini; do
		[ -f "$file" ] || continue
		is_generated "$file" || continue
		emit_overlay "$board" "$file" "$outdir"
	done
	;;
*)
	usage
	;;
esac
