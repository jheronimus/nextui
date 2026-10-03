#!/bin/sh
# check-boards.sh — static gate for the board configs in alpine/boards/.
#
# The kernel config fragments and the firmware blobs they reference are easy to
# desynchronise: delete a panel blob without editing CONFIG_EXTRA_FIRMWARE and
# the kernel build fails much later, at the point it stages the firmware, with
# an error that does not obviously point back here. This script fails fast and
# names both sides.
#
# POSIX sh, no project dependencies beyond this directory. Run from anywhere.

set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
COMMON="$ROOT/common"
GENTRAITS="$ROOT/gentraits.sh"
PKGS="$ROOT/../packages.txt"

# Boards we ship, in release matrix order.
BOARDS="h700 rk3566"

fail=0
note() { echo "  $*"; }
fail() {
	echo "  ERROR: $*" >&2
	fail=1
}

# The kernel build rewrites the __NEXTUI_*_FIRMWARE_DIR__ placeholders at build
# time, so CONFIG_EXTRA_FIRMWARE paths are relative to the board firmware dir
# first, then the common one.
firmware_dirs_for() {
	echo "$ROOT/$1/firmware $COMMON/firmware"
}

blob_exists() {
	for d in $2; do
		[ -e "$d/$1" ] && return 0
	done
	return 1
}

# Every blob listed across the board config and the base config must exist, and
# every blob on disk must be listed in one of them. config takes a label for
# error messages; `union` is the combined list from all configs for this board,
# used for the reverse (orphan) direction since a blob listed in the board
# config is legitimately absent from the base config and vice versa.
check_firmware() {
	board="$1"
	label="$2"
	config="$3"
	dirs="$4"
	union="$5"

	[ -f "$config" ] || {
		fail "$board: missing kernel config $label"
		return
	}

	value="$(sed -n 's/^CONFIG_EXTRA_FIRMWARE="\(.*\)"$/\1/p' "$config" | head -n 1)"
	if [ -z "$value" ]; then
		note "$board: $label lists no extra firmware (none needed)"
		return
	fi

	note "$board: $label references $(echo "$value" | wc -w | tr -d ' ') blobs"

	# Missing side: referenced but absent on disk. This is the failure that
	# surfaces confusingly at firmware-staging time.
	for blob in $value; do
		blob_exists "$blob" "$dirs" ||
			fail "$board: $label references '$blob' but it does not exist under: $dirs"
	done
}

# A blob on disk that no config lists never reaches the image. Usually means a
# device was dropped without updating the config, which is exactly what we did
# when pruning the H700 panel blobs.
check_orphan_firmware() {
	board="$1"
	dirs="$2"
	union="$3"

	for d in $dirs; do
		[ -d "$d" ] || continue
		for f in "$d"/*/* "$d"/*; do
			[ -f "$f" ] || continue
			rel="${f#"$d"/}"
			case " $union " in
			*" $rel "*) ;;
			*) fail "$board: '$rel' exists on disk but is in no CONFIG_EXTRA_FIRMWARE (orphan blob)" ;;
			esac
		done
	done
}

for board in $BOARDS; do
	dir="$ROOT/$board"
	echo "board: $board"

	if [ ! -d "$dir" ]; then
		fail "missing board directory $dir"
		continue
	fi

	# --- bootloader blobs ------------------------------------------------
	# These are vendored, not built (alpine/bootloader/README.md). Require them
	# here because their absence does not fail the image build loudly enough:
	# genimage still produces a .img.zst that simply cannot boot, which is the
	# most expensive way to find out.
	boot_dir="$(dirname "$ROOT")/bootloader/$board/out"
	if [ "$board" = "h700" ]; then
		boot_files="u-boot-sunxi-with-spl.bin u-boot-sunxi-with-spl-ddr3.bin"
	else
		boot_files="idbloader.img u-boot.itb"
	fi
	for bf in $boot_files; do
		if [ ! -f "$boot_dir/$bf" ]; then
			fail "missing vendored bootloader $boot_dir/$bf (see alpine/bootloader/README.md)"
		elif [ ! -s "$boot_dir/$bf" ]; then
			fail "bootloader $boot_dir/$bf is empty"
		fi
	done

	# --- kernel config fragments ------------------------------------------
	for cfg in tiny-base.config tiny-panfrost.config "tiny-$board.config"; do
		path="$dir/$cfg"
		case "$cfg" in
		tiny-base.config | tiny-panfrost.config) path="$COMMON/$cfg" ;;
		esac
		if [ ! -f "$path" ]; then
			fail "missing kernel config fragment $path"
		elif ! grep -q '^CONFIG_' "$path"; then
			fail "$path contains no CONFIG_ symbols"
		fi
	done

	# --- panfrost only ---------------------------------------------------
	# This fork is Alpine + Mesa panfrost. The libmali/Bifrost path is a
	# Buildroot thing and must not creep back in via config or patch series.
	if [ -f "$dir/tiny-$board.config" ]; then
		if grep -qE '^CONFIG_MALI_(KBASE|MIDGARD|BIFROST)=y' "$dir/tiny-$board.config"; then
			fail "$board: proprietary Mali driver enabled in config; panfrost-only"
		fi
	fi
	if [ -d "$dir/patches/linux" ]; then
		if grep -rlE 'CONFIG_MALI_(KBASE|BIFROST)' "$dir/patches/linux" >/dev/null 2>&1; then
			fail "$board: patch series touches proprietary Mali drivers; panfrost-only"
		fi
	fi
	if [ -f "$COMMON/tiny-libmali.config" ]; then
		fail "common/tiny-libmali.config present; this fork is Alpine/panfrost-only"
	fi

	# --- mandatory patch --------------------------------------------------
	# Without this, every input_* trait and the whole key_* table are fiction.
	# See $board/patches/linux/README.md.
	if [ ! -f "$dir/patches/linux/0025-input-name-devices-from-dt-node.patch" ]; then
		fail "$board: missing 0025-input-name-devices-from-dt-node.patch"
	fi

	# --- trait registry ---------------------------------------------------
	sh "$GENTRAITS" check "$board" || fail=1
	sh "$GENTRAITS" dtbs "$board" | while read -r dtb; do
		note "dtb $dtb"
	done

	# --- firmware ---------------------------------------------------------
	dirs="$(firmware_dirs_for "$board")"
	union=""
	for label in "tiny-$board.config" tiny-base.config; do
		case "$label" in
		tiny-base.config) config="$COMMON/tiny-base.config" ;;
		*) config="$dir/$label" ;;
		esac
		[ -f "$config" ] || continue
		union="$union $(sed -n 's/^CONFIG_EXTRA_FIRMWARE="\(.*\)"$/\1/p' "$config" | head -n 1)"
		check_firmware "$board" "$label" "$config" "$dirs" "$union"
	done
	check_orphan_firmware "$board" "$dirs" "$union"
done

# --- base package list ------------------------------------------------------

if [ -f "$PKGS" ]; then
	note "packages.txt: $(grep -cE '^[a-z0-9]' "$PKGS") entries"
	# musl build; gcompat is explicitly omitted upstream.
	if grep -qx 'gcompat' "$PKGS"; then
		fail "packages.txt: gcompat is intentionally omitted (musl build)"
	fi
	if grep -qx 'libmali' "$PKGS"; then
		fail "packages.txt: libmali is a Buildroot path; this fork uses Mesa panfrost"
	fi
	dup="$(grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$PKGS" | sort | uniq -d)"
	if [ -n "$dup" ]; then
		fail "packages.txt: duplicate entries: $(echo "$dup" | tr '\n' ' ')"
	fi

	# Every locally-built package must have an APKBUILD, and vice versa.
	# build.sh asserts the first direction before entering the chroot; this
	# asserts both, here, so a mismatch is a one-second failure not a
	# thirty-minute one.
	APORTS="$ROOT/../aports"
	claimed="$(sed -n '/^# --- Local packages/,$p' "$PKGS" |
		grep -vE '^[[:space:]]*#|^[[:space:]]*$' | tr '\n' ' ')"
	for p in $claimed; do
		[ -f "$APORTS/$p/APKBUILD" ] ||
			fail "packages.txt claims local package '$p' but alpine/aports/$p/APKBUILD is missing"
	done
	# The reverse: an aports/ dir nothing installs is dead weight, and usually
	# means someone added a package and forgot the two lists.
	if [ -d "$APORTS" ]; then
		for d in "$APORTS"/*; do
			[ -d "$d" ] || continue
			p="$(basename "$d")"
			[ "$p" = "tinykernel" ] && continue # built by build_tinykernel, not installed
			case " $claimed " in
			*" $p "*) ;;
			*) fail "alpine/aports/$p is built by nothing: add it to the Local packages section of alpine/packages.txt" ;;
			esac
		done
	fi
fi

# --- build chain wiring -----------------------------------------------------
#
# These are the places where a rename in one script silently breaks another.
# Cheap to check, very expensive to discover on a device.

BUILD_SH="$ROOT/../scripts/build.sh"
POST_BUILD_SH="$ROOT/../scripts/post-build.sh"
SYS_IMAGE_SH="$ROOT/../scripts/system-image.sh"
IMAGE_SH="$ROOT/../image/build.sh"

for s in "$BUILD_SH" "$POST_BUILD_SH" "$SYS_IMAGE_SH" "$IMAGE_SH"; do
	[ -f "$s" ] || fail "missing build script $s"
done

if [ -f "$BUILD_SH" ]; then
	# build_local_apks hardcodes the package list it builds; packages.txt
	# declares the list that is installed. If they diverge, apk installs a
	# package nobody built.
	# NOTE: [[:space:]] not \s -- BSD sed (macOS) does not understand \s, and a
	# silently-empty match here would make this check vacuous.
	built="$(sed -n 's/^[[:space:]]*ALPINE_PKGS="\(.*\)"$/\1/p' "$BUILD_SH" |
		tr -s '[:space:]' ' ' | sed 's/^ //; s/ $//')"
	claimed="$(sed -n '/^# --- Local packages/,$p' "$PKGS" |
		grep -vE '^[[:space:]]*#|^[[:space:]]*$' | tr '\n' ' ' | sed 's/^ //; s/ $//')"
	if [ -n "$claimed" ] && [ -z "$built" ]; then
		fail "could not read ALPINE_PKGS from build.sh; the package-list cross-check did not run"
	elif [ -n "$built" ] && [ "$built" != "$claimed" ]; then
		fail "packages.txt Local packages ('$claimed') != build.sh ALPINE_PKGS ('$built')"
	fi
fi

# The on-device staging root is .minime/ and is load-bearing: boot.cmd fatloads
# .minime/kernel and the overlay reads /mnt/sdcard/.minime/traits. If a rename
# changes it in one place, the device boots to nothing.
# Comment lines are stripped before grepping, otherwise the explanatory comment
# at the top of image/build.sh satisfies the check on its own.
if [ -f "$ROOT/common/boot.cmd" ] && [ -f "$IMAGE_SH" ]; then
	if ! grep -v '^[[:space:]]*#' "$ROOT/common/boot.cmd" | grep -q '\.minime/kernel'; then
		fail "boot.cmd no longer references .minime/kernel; the staging path changed and the boot path must be re-verified on hardware"
	fi
	if ! grep -v '^[[:space:]]*#' "$IMAGE_SH" | grep -q '\.minime/'; then
		fail "image/build.sh no longer stages to .minime/; it must match boot.cmd"
	fi
fi

if [ -f "$POST_BUILD_SH" ] && [ -f "$SYS_IMAGE_SH" ]; then
	# post-build.sh installs traits, system-image.sh copies them into the
	# initramfs. If either path changes, bootsplash/init.d/traits stop finding
	# them and the trait cascade silently never runs.
	post_path="$(sed -n 's|.*TARGET_DIR}/\(usr/share/[a-z]*/traits\).*|\1|p' "$POST_BUILD_SH" | head -1)"
	img_path="$(sed -n 's|.*TARGET_DIR}/\(usr/share/[a-z]*/traits\).*|\1|p' "$SYS_IMAGE_SH" | head -1)"
	if [ -n "$post_path" ] && [ -n "$img_path" ] && [ "$post_path" != "$img_path" ]; then
		fail "trait path mismatch: post-build.sh installs to '$post_path' but system-image.sh reads '$img_path'"
	fi
fi

# The on-device prefix /usr/share/minime and /mnt/sdcard/.minime is shared by
# the shell scripts and the C sources. Nothing checks this at build time, and a
# one-sided rename produces a device that boots and then silently loses its
# traits, its audio routing, or its screen geometry -- no error anywhere.
#
# Derive the set of prefixes actually used by the C code and require the shell
# side to agree. The C side is authoritative because traits.c cannot be changed
# without breaking compatibility with the image layout.
# The C sources that reference /usr/share paths live in the platform port, not
# in the foundation this script also validates. ROOT is alpine/boards, so the
# repo root is two levels up and the port is workspace/alpine inside it.
ALPINE_SRC="$(cd "$(dirname "$ROOT")/.." && pwd)"
PORT_DIR="$ALPINE_SRC/workspace/alpine"
SHIM_PLATFORM="$PORT_DIR/platform"
# Our own C sources, wherever /usr/share paths are referenced from. These live in
# the platform port; the vendored overlay under boards/ is Minime's code and is
# allowed to disagree with our scripts.
if [ -f "$SHIM_PLATFORM/traits.c" ]; then
	traits_path="$(sed -n 's/^#define TRAITS_PATH "\(.*\)"/\1/p' "$SHIM_PLATFORM/traits.c" | head -1)"
	[ -n "$traits_path" ] || fail "traits.c has no TRAITS_PATH define"
	case "$traits_path" in
	/mnt/sdcard/.minime/traits) ;;
	*)
		fail "traits.c TRAITS_PATH is '$traits_path'; the image layout and boot.cmd stage .minime/, so a change here must be verified on hardware"
		;;
	esac

	# Every /usr/share/<x> path the C code references must be installed by
	# post-build.sh, and vice versa.
	# Capture the FULL directory that follows ${TARGET_DIR}/, not just
	# /usr/share/<name>: traits and the helper scripts live in sibling
	# subdirectories, so a top-level test would let one satisfy the other.
	sh_dirs="$(sed -n 's|.*TARGET_DIR}/\(usr/share/[A-Za-z0-9_./-]*\).*|\1|p' "$POST_BUILD_SH" 2>/dev/null |
		sed 's|/$||' | sort -u | tr '\n' ' ')"
	# Collected into a variable first, then looped WITHOUT a pipe: a
	# `... | while read` loop runs in a subshell, so a failure detected inside
	# it cannot affect this script's exit status.
	# Scope: our own C sources. The vendored overlay under boards/ is Minime's
	# and does not need to agree with post-build.sh, so it is excluded.
	# Normalise to no leading slash on both sides: the C code writes
	# "/usr/share/minime/..." while the sed above captures "usr/share/minime"
	# (it follows ${TARGET_DIR}/).
	# Capture the WHOLE literal, not just the first two components: matching
	# only "/usr/share/<x>/" collapses "usr/share/minime/scripts/audio.sh" to
	# "usr/share/minime", which hides exactly the mismatch this check exists for.
	# C and headers only: post-build.sh itself mentions these paths, and
	# including it would make the check compare the shell side against itself.
	# '...' entries are prose in comments, not paths.
	# Scope to the port only. Sweeping the whole repo would pull in upstream's own
	# workspace/tg5050, tg5040 and desktop platform.c, which reference paths this
	# image never installs, and fail the check on someone else's code.
	c_paths="$(find "$PORT_DIR" -type f \( -name '*.c' -o -name '*.h' \) -print0 |
		xargs -0 grep -hoE '"/usr/share/[A-Za-z0-9_./-]+' 2>/dev/null |
		sed 's|"||; s|/$||; s|^/||' | grep -v '\.\.\.' | sort -u | tr '\n' ' ')"
	# cpath is a full path (usr/share/minime/scripts) and sh_dirs holds the
	# directories post-build.sh populates (usr/share/minime/scripts), so this
	# is a prefix test, not an equality test.
	for cpath in $c_paths; do
		[ -n "$cpath" ] || continue
		# Bidirectional prefix: the C path may name a directory the shell side
		# populates (usr/share/minine/scripts), or an ancestor of one
		# (usr/share/minime). Both are consistent; only a genuine mismatch
		# (usr/share/minime vs usr/share/nextui/...) should fail.
		ok=0
		for d in $sh_dirs; do
			case "$cpath" in
			"$d" | "$d"/* | usr/share/zoneinfo*)
				ok=1
				break
				;;
			esac
			case "$d" in
			"$cpath" | "$cpath"/*)
				ok=1
				break
				;;
			esac
		done
		[ "$ok" -eq 1 ] ||
			fail "C code references '$cpath' but post-build.sh installs nothing under it (installs: $sh_dirs)" "$c_paths"
	done
fi

if [ "$fail" -ne 0 ]; then
	echo "board check FAILED" >&2
	exit 1
fi
echo "board check passed"
