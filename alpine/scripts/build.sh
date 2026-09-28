#!/bin/sh
# shellcheck shell=sh disable=SC3043,SC3045
# NextUI Alpine components builder.
#
# Builds everything that goes into the OS side of the image:
#   1. Resolve the newest v3.24.x aarch64 minirootfs + verify its published
#      checksum against the official CDN.
#   2. Build local APKs (cross-compiled to aarch64-linux-musl) and tinykernel
#      against the same musl libc.
#   3. Assemble an Alpine rootfs from minirootfs + alpine/packages.txt, and
#      overlay the OpenRC services and board config.
#   4. Produce system.erofs and initramfs.img for the packager.
#
# This does NOT build the NextUI payload. That is the workspace/ makefile's job
# (see Taskfile: `task payload`); image/build.sh stitches the two together.
#
# Derived from Minime's packages/components/alpine/scripts/build.sh. Changes:
#   * no UI concept (NextUI is the only UI)
#   * no blast/LÖVE package set, no test package set
#   * one package list (alpine/packages.txt) instead of world-common +
#     world-<board>; our two boards need no per-board package delta
#   * no prebuilt-kernel download from a GitHub release
#   * bootsplash / remote not built (see alpine/aports/README.md)
#   * rk3326 removed
#
# Environment:
#   BOARD            Target board (h700 | rk3566). Required in practice.
#   ALPINE_JOBS      Parallel build jobs (default: nproc).
#   ALPINE_DIR       Path to the alpine/ source tree.
#   NEXTUI_ROOT      Repo root. Derived from ALPINE_DIR if unset.

set -eu

ulimit -n 65536 2>/dev/null || true

# Wrapper to allow abuild to run as root within the container.
abuild() {
	/usr/bin/abuild -F "$@"
}

ALPINE_BRANCH="v3.24"
ALPINE_ARCH="aarch64"
ALPINE_MINIROOTFS_BASE_URL="https://dl-cdn.alpinelinux.org/alpine/${ALPINE_BRANCH}/releases/${ALPINE_ARCH}"

# Output directories (the container maps these to host bind volumes).
ALPINE_OUTPUT_DIR="${ALPINE_OUTPUT_DIR:-/alpine-output}"
ALPINE_BUILD_DIR="${ALPINE_BUILD_DIR:-${ALPINE_OUTPUT_DIR}/build}"
ALPINE_PACKAGES_DIR="${ALPINE_PACKAGES_DIR:-${ALPINE_OUTPUT_DIR}/packages/main}"
ALPINE_ROOTFS_DIR="${ALPINE_ROOTFS_DIR:-${ALPINE_OUTPUT_DIR}/rootfs}"
ALPINE_DL_DIR="${ALPINE_DL_DIR:-/alpine-dl/src}"

ALPINE_DIR="${ALPINE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
NEXTUI_ROOT="${NEXTUI_ROOT:-$(cd "${ALPINE_DIR}/.." && pwd)}"

BOARD="${BOARD:-rk3566}"
ALPINE_JOBS="${ALPINE_JOBS:-$(nproc 2>/dev/null || echo 4)}"

log() { printf '[alpine] %s\n' "$*" >&2; }
die() {
	log "ERROR: $*"
	exit 1
}

case "${BOARD}" in
rk3566 | h700) ;;
*) die "unsupported BOARD=${BOARD} (supported: h700, rk3566)" ;;
esac
export DTB_BOARD="${BOARD}"

# macOS Docker Desktop inherits the host kernel's per-process fd limit
# (kern.maxfilesperproc, default 61440). The Linux kernel build opens thousands
# of files and will fail with "No file descriptors available" once that ceiling
# is hit. Warn early so the user can raise it:
#   sudo sysctl kern.maxfilesperproc=1048576 kern.maxfiles=1048576
if [ "$(uname)" = "Linux" ] && grep -qi docker /proc/1/cgroup 2>/dev/null; then
	fd_limit=$(ulimit -n 2>/dev/null || echo 0)
	if [ "$fd_limit" -lt 100000 ] 2>/dev/null; then
		log "WARNING: per-process fd limit is ${fd_limit} - kernel build may fail."
		log "  On macOS host, run: sudo sysctl kern.maxfilesperproc=1048576 kern.maxfiles=1048576"
	fi
fi

#──────────────────────────────────────────────────────────────────────────────
# 1. Resolve + verify minirootfs
#──────────────────────────────────────────────────────────────────────────────

resolve_minirootfs() {
	mkdir -p "${ALPINE_DL_DIR}"
	mm_index="${ALPINE_DL_DIR}/.latest-releases"
	curl -fsSL --retry 3 "${ALPINE_MINIROOTFS_BASE_URL}/latest-releases.yaml" \
		-o "${mm_index}" || die "could not fetch latest-releases.yaml"

	# latest-releases.yaml is a list of release entries; the minirootfs entry
	# has flavor: alpine-minirootfs. Each entry begins with a `-` on its own
	# line. Inside each entry, `version:` is listed before `flavor:` (so we
	# cannot "look forward" from the flavor to find the version), and `sha256:`
	# is listed after `flavor:`. Use the `-` separator to clear per-entry state,
	# capture the version as it appears, then print it on the matching flavor.
	mm_version=$(awk '
		/^-[[:space:]]*$/ { in_mini = 0; mm_version = "" }
		!in_mini && /version:/ {
			sub(/^[[:space:]]*version:[[:space:]]*/, "")
			mm_version = $0
		}
		/flavor:[[:space:]]*alpine-minirootfs/ {
			if (mm_version != "") { print mm_version; exit }
		}
	' "${mm_index}") || die "no minirootfs version in latest-releases.yaml"
	mm_sha=$(awk '
		/^-[[:space:]]*$/ { in_mini = 0 }
		/flavor:[[:space:]]*alpine-minirootfs/ { in_mini = 1 }
		in_mini && /sha256:/ {
			sub(/^[[:space:]]*sha256:[[:space:]]*/, "")
			print
			exit
		}
	' "${mm_index}") || die "no minirootfs sha256 in latest-releases.yaml"

	case "${mm_version}" in
	${ALPINE_BRANCH#v}.*) ;;
	*) die "this fork is locked to ${ALPINE_BRANCH}; got ${mm_version}" ;;
	esac

	mm_tar="alpine-minirootfs-${mm_version}-${ALPINE_ARCH}.tar.gz"
	mm_path="${ALPINE_DL_DIR}/${mm_tar}"
	mm_url="${ALPINE_MINIROOTFS_BASE_URL}/${mm_tar}"

	if [ ! -f "${mm_path}" ]; then
		log "downloading ${mm_url}"
		curl -fL --retry 3 -o "${mm_path}" "${mm_url}" ||
			die "download failed: ${mm_url}"
	fi

	mm_got=$(sha256sum "${mm_path}" | awk '{print $1}')
	[ "${mm_got}" = "${mm_sha}" ] ||
		die "minirootfs sha256 mismatch (want ${mm_sha}, got ${mm_got})"

	log "minirootfs: ${mm_version} (sha256 $(printf '%s' "${mm_got}" | cut -c1-12)...)"
	MINIROOTFS_TAR="${mm_path}"
}

#──────────────────────────────────────────────────────────────────────────────
# 2. Build local APKs
#──────────────────────────────────────────────────────────────────────────────

build_local_apks() {
	# abuild's CBUILD = host, CHOST = aarch64-linux-musl so the package's
	# build() runs with the cross toolchain. Each APKBUILD opts in by setting
	# `arch="aarch64"` and declaring `makedepends` of gcc-aarch64/binutils-aarch64.
	CBUILD="$(uname -m)-alpine-linux-musl"
	CHOST="aarch64-alpine-linux-musl"
	CARCH="aarch64"
	REPODEST="${ALPINE_PACKAGES_DIR}"
	export CBUILD CHOST CARCH REPODEST

	mkdir -p "${ALPINE_PACKAGES_DIR}" "${ALPINE_BUILD_DIR}"

	log "updating apk repositories index..."
	sudo apk update

	# tinykernel is built separately because it drives the host kernel toolchain
	# (not the cross-compiler) and is staged into the SD image directly, not
	# installed as a rootfs package.
	build_tinykernel

	# Local packages that go into the rootfs. Keep this list in sync with the
	# "Local packages" section of alpine/packages.txt; check-boards.sh does not
	# verify it, so if you add one there, add it here.
	ALPINE_PKGS="fatresize mdnsd"

	for ALPINE_PKG in ${ALPINE_PKGS}; do
		[ -d "${ALPINE_DIR}/aports/${ALPINE_PKG}" ] || die "missing aports/${ALPINE_PKG}"
		cd "${ALPINE_DIR}/aports/${ALPINE_PKG}"
		log "abuild: ${ALPINE_PKG}"
		# abuild does not have its own -j flag; pass MAKEFLAGS so the inner make
		# runs in parallel. -f forces a full rebuild even if the per-package
		# stamp looks up to date (defends against stale state from interrupted
		# runs).
		MAKEFLAGS="-j${ALPINE_JOBS}" \
			abuild -r -P "${ALPINE_PACKAGES_DIR}" -D "${ALPINE_DL_DIR}" -c
	done
}

# Prime the kernel source tarball into $SRCDEST with a retrying curl before
# abuild sees it. abuild-fetch performs a single download attempt with no retry
# and aborts the whole build when a transient CDN truncation fails the sha512
# check; pre-downloading and verifying here makes that failure mode self-healing.
ensure_kernel_tarball() {
	tk_sha=$(sed -n 's/^sha512sums="\([0-9a-f]\{128\}\).*/\1/p' "${TK_APKB}")
	[ -n "${tk_sha}" ] || die "tinykernel APKBUILD has no sha512sums"
	name="linux-${tk_ver}.tar.xz"
	url="https://cdn.kernel.org/pub/linux/kernel/v${tk_ver%%.*}.x/${name}"
	dest="${ALPINE_DL_DIR}/${name}"
	mkdir -p "${ALPINE_DL_DIR}"

	if [ -f "${dest}" ] && printf '%s  %s\n' "${tk_sha}" "${dest}" | sha512sum -c - >/dev/null 2>&1; then
		log "kernel tarball cached: ${name}"
		return 0
	fi

	tries=0
	while [ "${tries}" -lt 4 ]; do
		tries=$((tries + 1))
		log "downloading ${url} (attempt ${tries}/4)"
		if curl -fL --retry 3 --retry-all-errors --retry-delay 2 -o "${dest}.part" "${url}" &&
			printf '%s  %s\n' "${tk_sha}" "${dest}.part" | sha512sum -c - >/dev/null 2>&1; then
			mv -f "${dest}.part" "${dest}"
			log "kernel tarball verified: ${name}"
			return 0
		fi
		rm -f "${dest}.part"
		log "kernel tarball download/checksum failed; retrying"
	done
	die "could not download ${name} (sha512 ${tk_sha})"
}

build_tinykernel() {
	TK_APKB="${ALPINE_DIR}/aports/tinykernel/APKBUILD"
	[ -f "${TK_APKB}" ] || die "missing aports/tinykernel/APKBUILD"

	tk_ver=$(sed -n 's/^pkgver=//p' "${TK_APKB}")
	tk_rel=$(sed -n 's/^pkgrel=//p' "${TK_APKB}")
	board_apk_name="tinykernel-${BOARD}-${tk_ver}-r${tk_rel}.apk"

	mkdir -p "${ALPINE_PACKAGES_DIR}/build/aarch64"
	apk_file="${ALPINE_PACKAGES_DIR}/build/aarch64/tinykernel-${tk_ver}-r${tk_rel}.apk"

	need_build=1
	if [ "${FORCE_KERNEL_REBUILD:-0}" = "1" ] || [ "${FORCE_KERNEL_REBUILD:-0}" = "true" ]; then
		log "FORCE_KERNEL_REBUILD requested; compiling tinykernel"
	elif [ -f "${apk_file}" ]; then
		log "Reusing cached tinykernel APK: ${apk_file}"
		need_build=0
	else
		log "Compiling tinykernel"
	fi

	if [ "${need_build}" = "1" ]; then
		# Wipe the build dir first: an interrupted rootpkg run leaves
		# fakeroot-owned files in src/ and pkg/ that the agent user cannot
		# delete; abuild's up-to-date cache will then skip the unpack step and
		# the package stage fails with "can't cd to src/<name>".
		rm -rf "${ALPINE_BUILD_DIR}/tinykernel" 2>/dev/null ||
			die "could not clear ${ALPINE_BUILD_DIR}/tinykernel; rm as root first"
		mkdir -p "${ALPINE_BUILD_DIR}/tinykernel"
		cp -a "${ALPINE_DIR}/aports/tinykernel/." "${ALPINE_BUILD_DIR}/tinykernel/"
		cd "${ALPINE_BUILD_DIR}/tinykernel"
		sed -i 's/gcc-aarch64//g; s/binutils-aarch64//g; s/CROSS_COMPILE=aarch64-alpine-linux-musl-/CROSS_COMPILE=/g' APKBUILD

		log "abuild: tinykernel (${BOARD})"
		ensure_kernel_tarball
		JOBS="${ALPINE_JOBS:-2}" MAKEFLAGS="-j${ALPINE_JOBS:-2}" \
			abuild -r -P "${ALPINE_PACKAGES_DIR}" -D "${ALPINE_DL_DIR}"
	fi

	# Stage the kernel artifacts for the packager to consume.
	[ -f "${apk_file}" ] || die "tinykernel did not produce APK at ${apk_file}"
	log "staging tinykernel from APK: ${apk_file}"
	mkdir -p "${ALPINE_OUTPUT_DIR}/boot"
	rm -rf "${ALPINE_OUTPUT_DIR}/boot/lib" "${ALPINE_OUTPUT_DIR}/boot/var" "${ALPINE_OUTPUT_DIR}/boot/dtbs"
	tar -xzf "${apk_file}" -C "${ALPINE_OUTPUT_DIR}/boot"
	mv -f "${ALPINE_OUTPUT_DIR}/boot/var/lib/nextui/tinykernel.Image" "${ALPINE_OUTPUT_DIR}/boot/Image"
	mv -f "${ALPINE_OUTPUT_DIR}/boot/var/lib/nextui/dtbs" "${ALPINE_OUTPUT_DIR}/boot/dtbs"
	rm -rf "${ALPINE_OUTPUT_DIR}/boot/var"
	log "tinykernel staged: ${ALPINE_OUTPUT_DIR}/boot/Image and DTBs"

	# Copy the APK to the target out directory for release packaging/upload.
	target_out="${ALPINE_DIR}/out/${BOARD}"
	mkdir -p "${target_out}"
	cp -f "${apk_file}" "${target_out}/${board_apk_name}"
}

#──────────────────────────────────────────────────────────────────────────────
# 3. Assemble Alpine rootfs (minirootfs + packages.txt + overlay)
#──────────────────────────────────────────────────────────────────────────────

assemble_rootfs() {
	PKG_LIST="${ALPINE_DIR}/packages.txt"
	[ -f "${PKG_LIST}" ] || die "missing ${PKG_LIST}"

	umount -lf "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || true
	umount -lf "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || true
	umount -lf "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || true
	chmod -R +w "${ALPINE_ROOTFS_DIR}" 2>/dev/null || true
	rm -rf "${ALPINE_ROOTFS_DIR}" 2>/dev/null || true
	mkdir -p "${ALPINE_ROOTFS_DIR}"
	tar -xf "${MINIROOTFS_TAR}" -C "${ALPINE_ROOTFS_DIR}"

	# Build a local aports index so apk can resolve locally-built packages from
	# the same repo as the official Alpine packages.
	# The minirootfs URL is <base>/releases/<arch>; the apk repos are <base>/main
	# and <base>/community. Build <base> directly rather than stripping a
	# suffix off the minirootfs URL.
	ALPINE_REPO_BASE="https://dl-cdn.alpinelinux.org/alpine/${ALPINE_BRANCH}"
	cat >"${ALPINE_ROOTFS_DIR}/etc/apk/repositories" <<-EOF
		${ALPINE_REPO_BASE}/main
		${ALPINE_REPO_BASE}/community
		/local-repo
	EOF

	# Stage the local repo inside the rootfs so `apk add` inside the chroot can
	# resolve our packages.
	mkdir -p "${ALPINE_ROOTFS_DIR}/local-repo/aarch64"
	find "${ALPINE_PACKAGES_DIR}" -name '*.apk' -exec cp -f {} "${ALPINE_ROOTFS_DIR}/local-repo/aarch64/" \;
	if ls "${ALPINE_ROOTFS_DIR}/local-repo/aarch64/"*.apk >/dev/null 2>&1; then
		(cd "${ALPINE_ROOTFS_DIR}/local-repo/aarch64" && apk index -o APKINDEX.tar.gz ./*.apk)
	fi

	# Resolve the package list. One list for both boards: Minime keeps a
	# per-board world-<board> overlay, but theirs are empty for h700 and rk3566
	# because the board deltas are in-kernel, not userspace.
	WORLD_PKGS="$(grep -vE '^[[:space:]]*#|^[[:space:]]*$' "${PKG_LIST}" | tr '\n' ' ')"
	[ -n "${WORLD_PKGS}" ] || die "resolved package list is empty from ${PKG_LIST}"

	# Guard: every locally-built package named in packages.txt must exist in
	# alpine/aports/, or apk will fail deep inside the chroot with an unhelpful
	# "unable to select packages" message.
	for local_pkg in $(sed -n '/^# --- Local packages/,$p' "${PKG_LIST}" |
		grep -vE '^[[:space:]]*#|^[[:space:]]*$' | tr '\n' ' '); do
		[ -d "${ALPINE_DIR}/aports/${local_pkg}" ] ||
			die "packages.txt lists local package '${local_pkg}' but alpine/aports/${local_pkg} does not exist"
	done

	cp /etc/resolv.conf "${ALPINE_ROOTFS_DIR}/etc/resolv.conf" 2>/dev/null || true
	mount -t proc proc "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || mount --bind /proc "${ALPINE_ROOTFS_DIR}/proc"
	mount -t sysfs sysfs "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || mount --bind /sys "${ALPINE_ROOTFS_DIR}/sys"
	mount -t tmpfs tmpfs "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || mount --bind /dev "${ALPINE_ROOTFS_DIR}/dev"
	trap 'umount -f -R "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || true; umount -f -R "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || true; umount -f -R "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || true' EXIT

	# Install packages via chroot so apk run triggers fire (font caches, etc.).
	# ${WORLD_PKGS} is intentionally unquoted: it is a whitespace-separated
	# list and apk needs one argv entry per package.
	# shellcheck disable=SC2086
	chroot "${ALPINE_ROOTFS_DIR}" /sbin/apk add \
		--no-cache --allow-untrusted --force-overwrite ${WORLD_PKGS}

	# Overlay OpenRC services, system config, udev rules, board traits.
	TARGET_DIR="${ALPINE_ROOTFS_DIR}" "${ALPINE_DIR}/scripts/post-build.sh" -b "${BOARD}"

	# No authentication model: telnet uses an autologin shell and dropbear
	# allows blank-password root logins. Clear root's password so `ssh <host>`
	# and `telnet <host>` both connect without auth.
	chroot "${ALPINE_ROOTFS_DIR}" /bin/sh -c \
		"passwd -d root 2>/dev/null || sed -i 's/^root:.*/root::0:0:root:\/root:\/bin\/sh/' /etc/shadow" 2>/dev/null || true

	# Install the tinykernel modules into the immutable EROFS rootfs.
	if [ -d "${ALPINE_OUTPUT_DIR}/boot/lib/modules" ]; then
		cp -a "${ALPINE_OUTPUT_DIR}/boot/lib/modules/." "${ALPINE_ROOTFS_DIR}/lib/modules/"
		TK_KVER=$(find "${ALPINE_ROOTFS_DIR}/lib/modules" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | head -1)
		TK_KVER=$(basename "$TK_KVER" 2>/dev/null || true)
		if [ -n "${TK_KVER}" ]; then
			chroot "${ALPINE_ROOTFS_DIR}" \
				/sbin/depmod -a "${TK_KVER}" 2>/dev/null || true
		fi
	fi

	# Unmount bind-mounted pseudo filesystems.
	umount -f -R "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/sys" 2>/dev/null || true
	umount -f -R "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/proc" 2>/dev/null || true
	umount -f -R "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || umount -lf "${ALPINE_ROOTFS_DIR}/dev" 2>/dev/null || true
	trap - EXIT

	# Remove transient build staging and the local apk repo.
	rm -rf "${ALPINE_ROOTFS_DIR}/local-repo"
	sed -i '\|/local-repo|d' "${ALPINE_ROOTFS_DIR}/etc/apk/repositories" 2>/dev/null || true
}

#──────────────────────────────────────────────────────────────────────────────
# 4. Build system image (erofs + initramfs)
#──────────────────────────────────────────────────────────────────────────────

build_system_image() {
	[ -d "${ALPINE_ROOTFS_DIR}" ] || assemble_rootfs
	TARGET_OUT="${ALPINE_DIR}/out/${BOARD}"
	mkdir -p "${TARGET_OUT}"

	cp -f "${ALPINE_OUTPUT_DIR}/boot/Image" "${TARGET_OUT}/Image"
	[ -f "${TARGET_OUT}/Image" ] || die "kernel Image missing in ${ALPINE_OUTPUT_DIR}/boot/"

	[ -d "${ALPINE_OUTPUT_DIR}/boot/dtbs" ] || die "kernel DTBs missing in ${ALPINE_OUTPUT_DIR}/boot/dtbs"
	find "${ALPINE_OUTPUT_DIR}/boot/dtbs" -name '*.dtb' -exec cp -f {} "${TARGET_OUT}/" \;

	TARGET_DIR="${ALPINE_ROOTFS_DIR}" BINARIES_DIR="${TARGET_OUT}" \
		"${ALPINE_DIR}/scripts/system-image.sh" -b "${BOARD}"

	log "system image: ${TARGET_OUT}"
}

#──────────────────────────────────────────────────────────────────────────────
# Entrypoint
#──────────────────────────────────────────────────────────────────────────────

CMD="${1:-components}"
case "${CMD}" in
components)
	resolve_minirootfs
	build_local_apks
	assemble_rootfs
	build_system_image
	;;
minirootfs) resolve_minirootfs ;;
apks) build_local_apks ;;
rootfs) assemble_rootfs ;;
system-image) build_system_image ;;
shell) exec /bin/sh ;;
*) die "unknown subcommand: ${CMD} (use components|minirootfs|apks|rootfs|system-image|shell)" ;;
esac
