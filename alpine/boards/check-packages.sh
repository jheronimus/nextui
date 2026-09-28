#!/bin/sh
# Verify every Alpine package this project installs actually exists.
#
# This exists because a typo in a package name only surfaces as a container
# build failure several minutes into a CI run, after the runner has been
# paid for. `apk add` is the only thing that knows the answer, so check the
# real APKINDEX instead of guessing.
#
# Sources checked:
#   - every `apk add` package list in the builder Dockerfile
#   - workspace/alpine/packages.txt, the shared rootfs selection
#   - makedepends/builddeps in the APKBUILDs we vendor
#
# Usage: check-packages.sh [--cached]
#   --cached  reuse a previously downloaded APKINDEX in $ALPINE_PKG_CACHE

set -eu

ALPINE_ROOT="${ALPINE_ROOT:-$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)}"
ALPINE_DIR="${ALPINE_DIR:-${ALPINE_ROOT}/alpine}"
ALPINE_PKG_CACHE="${ALPINE_PKG_CACHE:-${TMPDIR:-/tmp}/alpine-apkindex}"
BRANCH="${ALPINE_BRANCH:-v3.24}"
ARCH="${ALPINE_ARCH:-aarch64}"
REPOS="${ALPINE_REPOS:-main community}"
MIRROR="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"

use_cache=0
[ "${1:-}" = "--cached" ] && use_cache=1

index_files=""
n_index=0

fetch_index() {
	repo="$1"
	tgz="${ALPINE_PKG_CACHE}/APKINDEX-${BRANCH}-${repo}-${ARCH}.tar.gz"

	if [ "${use_cache}" -eq 1 ] && [ -f "${tgz}" ]; then
		:
	else
		mkdir -p "${ALPINE_PKG_CACHE}"
		url="${MIRROR}/${BRANCH}/${repo}/${ARCH}/APKINDEX.tar.gz"
		if ! curl -fsSL --connect-timeout 10 --max-time 30 --retry 3 -o "${tgz}" "${url}"; then
			echo "check-packages: WARNING: cannot fetch ${url}, skipping ${repo}" >&2
			return 0
		fi
	fi

	out="${ALPINE_PKG_CACHE}/IDX-${BRANCH}-${repo}-${ARCH}"
	# Only the APKINDEX member is needed; the signature is Alpine's to verify.
	tar xzf "${tgz}" -C "${ALPINE_PKG_CACHE}" APKINDEX 2>/dev/null || return 0
	mv "${ALPINE_PKG_CACHE}/APKINDEX" "${out}"
	index_files="${index_files} ${out}"
	n_index=$((n_index + 1))
}

for repo in ${REPOS}; do
	fetch_index "${repo}"
done

if [ "${n_index}" -eq 0 ]; then
	echo "check-packages: no APKINDEX available, cannot verify packages" >&2
	exit 1
fi

# Every "P:<name>" record in the index, one per line.
# shellcheck disable=SC2086 # index_files is an intentional word list here.
available="$(cat ${index_files} 2>/dev/null | sed -n 's/^P://p' | sort -u)"

echo "check-packages: ${BRANCH}/${ARCH} repos [${REPOS}]"

# Packages this repo builds from its own aports. These are not in any Alpine
# index by design, so they are checked against our aports instead.
local_pkgs="$(grep -h '^pkgname=' "${ALPINE_DIR}"/aports/*/APKBUILD 2>/dev/null |
	sed 's/^pkgname=//' | grep -v '^$' | sort -u || true)"
if [ -n "${local_pkgs}" ]; then
	n_local="$(printf '%s\n' "${local_pkgs}" | grep -c . || true)"
	echo "check-packages: ${n_local} package(s) built from our aports: $(printf '%s\n' "${local_pkgs}" | tr '\n' ' ')"
fi

# Collect the package names this project asks apk to install.
wanted=""

# apk add / apk del lists inside Dockerfiles: continuation lines starting with
# a tab, with flags and comments stripped.
for df in "${ALPINE_DIR}"/container/Dockerfile "${ALPINE_DIR}"/aports/*/Dockerfile; do
	[ -f "${df}" ] || continue
	list="$(awk '
		/apk[ \t]+(add|del)/ {
			inlist = 1
			line = $0
			sub(/.*apk[ \t]+(add|del)[ \t]*/, "", line)
			print line
			next
		}
		inlist && /\\$/ {
			line = $0
			sub(/\\$/, "", line)
			print line
			next
		}
		inlist { inlist = 0 }
	' "${df}" | tr '\n\t ' '\n' |
		grep -v '^$' |
		grep -v '^-' |
		grep -v '^--' |
		grep -v '^#' |
		grep -v '^\\$' || true)"
	if [ -n "${list}" ]; then
		wanted="${wanted}
${list}"
	fi
done

# The shared rootfs selection, one package per line.
if [ -f "${ALPINE_DIR}/packages.txt" ]; then
	list="$(sed 's/#.*//' "${ALPINE_DIR}/packages.txt" | tr '\n\t ' '\n' | grep -v '^$' || true)"
	if [ -n "${list}" ]; then
		wanted="${wanted}
${list}"
	fi
fi

# makedepends/builddeps in vendored APKBUILDs.
for apkbuild in "${ALPINE_DIR}"/aports/*/APKBUILD; do
	[ -f "${apkbuild}" ] || continue
	list="$(sed -n 's/^\(makedepends\|builddeps\|depends\)="\(.*\)"$/\2/p' "${apkbuild}" |
		tr '\n\t ' '\n' | grep -v '^$' | grep -v '^!' || true)"
	if [ -n "${list}" ]; then
		wanted="${wanted}
${list}"
	fi
done

wanted="$(printf '%s\n' "${wanted}" | sed 's/[<>=~][0-9][^ ]*$//' | grep -v '^$' | sort -u || true)"

# Split into packages Alpine must provide and packages we build ourselves.
alpine_wanted=""
local_wanted=""
for pkg in ${wanted}; do
	if printf '%s\n' "${local_pkgs}" | grep -qxF "${pkg}"; then
		local_wanted="${local_wanted} ${pkg}"
	else
		alpine_wanted="${alpine_wanted} ${pkg}"
	fi
done
wanted="${alpine_wanted# }"

if [ -z "${wanted}" ]; then
	echo "check-packages: no packages found to check -- is the layout intact?" >&2
	exit 1
fi

echo "check-packages: checking $(printf '%s' "${alpine_wanted}" | wc -w | tr -d ' ') Alpine package names" \
	"(+$(printf '%s' "${local_wanted}" | wc -w | tr -d ' ') from our aports)"

missing=""
for pkg in ${alpine_wanted}; do
	if ! printf '%s\n' "${available}" | grep -qxF "${pkg}"; then
		missing="${missing}
  ${pkg}"
	fi
done

n_wanted="$(printf '%s' "${alpine_wanted}" | wc -w | tr -d ' ')"

if [ -n "${missing}" ]; then
	echo "check-packages: these package names do not exist in ${BRANCH}/${ARCH}:" >&2
	printf '%s\n' "${missing}" >&2
	echo "" >&2
	echo "check-packages: ${n_wanted} checked, $(printf '%s\n' "${missing}" | grep -c . || true) missing" >&2
	echo "" >&2
	echo "Look the real name up before editing the Dockerfile; guessing costs a" >&2
	echo "full container build in CI to discover the same thing again." >&2
	exit 1
fi

echo "check-packages: all ${n_wanted} Alpine package names exist"
echo "check-packages: passed"
