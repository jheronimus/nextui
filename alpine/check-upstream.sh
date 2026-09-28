#!/bin/sh
# check-upstream.sh — enforce this fork's core invariant.
#
# The invariant: relative to the NextUI base tag, the ONLY files this repo may
# add or modify live in the two Alpine trees and the root Taskfile.yml:
#   alpine/**            the Alpine foundation (kernel, rootfs, bootloader, DTS)
#   workspace/alpine/**  the NextUI platform port for alpine
# Every other file must be byte-identical to upstream.
#
# Why this is checked rather than trusted: the whole point of the layout is
# that a future NextUI release can be merged as a normal `git merge` with no
# conflicts. The moment someone edits makefile, .gitignore, skeleton/ or
# workspace/all/, that property quietly stops being true, and the next upstream
# release becomes a manual merge instead of a fast-forward.
#
# Note this is strictly stronger than "we do not fork NextUI". It also means we
# cannot add files at the repo root other than Taskfile.yml, so the Alpine
# layer cannot grow a stray script or a second makefile there.
#
# POSIX sh. Run from anywhere.

# fail() exits, so shellcheck cannot see that the lines after each call are
# reachable from the enclosing if. The pattern is fine; silence the noise.
# shellcheck disable=SC2317

set -eu

# The NextUI release this fork is based on.
BASE="${NEXTUI_BASE:-v6.14.0}"

# Where we are allowed to add NEW files.
#   alpine/**                     the Alpine foundation
#   workspace/alpine/**           the NextUI platform port
#   .github/workflows/alpine-*.yml our CI (GitHub only reads the repo root, and
#                                 upstream has no file by this name, so a future
#                                 upstream release can never conflict with it)
#   Taskfile.yml                  the build entry point
ALLOWED_NEW="alpine/ workspace/alpine/ .github/workflows/alpine- Taskfile.yml"

# Where we are allowed to MODIFY or DELETE existing NextUI files. Deliberately
# narrower than ALLOWED_NEW: additions cannot conflict with an upstream merge,
# modifications always can. This is the rule that keeps `git merge upstream/main`
# a fast-forward instead of a manual merge.
ALLOWED_MOD="alpine/ workspace/alpine/ Taskfile.yml"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

log() { printf '  %s\n' "$*"; }

# fail <message> [detail-list]
#
# The detail list is printed here rather than at the call site because this
# function exits, so anything written after the call would never run -- which is
# exactly the kind of silent truncation that makes a failing check useless.
fail() {
	printf '  ERROR: %s\n' "$1" >&2
	shift || true
	if [ "$#" -gt 0 ]; then
		for f in $1; do printf '           %s\n' "$f" >&2; done
	fi
	exit 1
}

if ! git rev-parse --verify "$BASE" >/dev/null 2>&1; then
	echo "  ERROR: base tag '$BASE' not found in this clone." >&2
	echo "         fetch it first:  git fetch --tags upstream" >&2
	exit 1
fi

# allowed <path> <allowlist>
allowed() {
	for p in $2; do
		case "$1" in
		"$p" | "$p"*) return 0 ;;
		esac
	done
	return 1
}

# --- every path that differs from the base, committed or not ---------------
#
# `git diff --name-only $BASE` only sees committed changes, so on a fresh
# working tree it reports nothing at all. The untracked listing is unioned in so
# a file that exists but has not been `git add`ed is still checked.

# Committed changes to files upstream also has: these are MODIFICATIONS.
# --diff-filter is essential: a plain `git diff --name-only` also lists files
# this fork ADDED, which would then be judged as modifications of upstream files
# that do not exist. M = modified, A = added, D = deleted.
modified="$(git diff --name-only --diff-filter=M "$BASE" -- . || true)"
added="$(git diff --name-only --diff-filter=A "$BASE" -- . || true)"

# Files that exist here but not in the base: these are ADDITIONS. An addition
# cannot conflict with a future upstream merge unless upstream later creates a
# file at the same path, which is why the allowlist is explicit rather than open.
untracked="$(git ls-files --others --exclude-standard || true)"
added="$added
$untracked"

mod_violations=""
for f in $modified; do
	allowed "$f" "$ALLOWED_MOD" || mod_violations="$mod_violations $f"
done
if [ -n "$mod_violations" ]; then
	fail "these existing NextUI files are modified. Only ${ALLOWED_MOD} may be modified, or a future upstream release cannot fast-forward:" "$mod_violations"
fi

new_violations=""
for f in $untracked; do
	allowed "$f" "$ALLOWED_NEW" || new_violations="$new_violations $f"
done
if [ -n "$new_violations" ]; then
	fail "these new files are outside the allowed paths (${ALLOWED_NEW}):" "$new_violations"
fi

n_mod=$(echo "$modified" | grep -c . || true)
n_new=$(echo "$added" | grep -c . || true)
log "$n_mod modified, $n_new added -- all within the allowed paths"

# --- deletions --------------------------------------------------------------
#
# Deleting a NextUI file is an edit. workspace/all/ and skeleton/ in particular
# are shared with the TrimUI platforms we keep, so removing a file from them
# breaks those builds.

deleted="$(git diff --name-only --diff-filter=D "$BASE" -- . || true)"
if [ -n "$deleted" ]; then
	fail "these upstream files are deleted:" "$deleted"
fi

# --- stray files at the repo root -------------------------------------------
#
# Catches a new file dropped next to the makefile instead of inside
# workspace/alpine/. `git ls-files` is committed state; the untracked listing
# catches a file that was created but not added yet.

root_stray=""
for f in $(git status --porcelain | awk '{print $NF}' | grep -vE '^workspace/alpine/|^Taskfile\.yml$|^$'); do
	case "$f" in
	*/*) ;; # not at the root
	*) root_stray="$root_stray $f" ;;
	esac
done
if [ -n "$root_stray" ]; then
	fail "new files at the repo root are not allowed; put them in workspace/alpine/:" "$root_stray"
fi

echo "upstream check passed (base: $BASE)"
