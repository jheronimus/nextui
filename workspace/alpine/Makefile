# NextUI Alpine build.
#
# This is the only makefile the Alpine layer owns. It is the leaf driver behind
# the Taskfile, which is the entry point for everything.
#
# It does two jobs:
#
#   1. OS image  (components / image / builder / clean)
#      The kernel, rootfs and genimage packaging, ported from Minime.
#
#   2. Payload   (payload / system / package)
#      Compiling NextUI itself for the alpine platform, and assembling the
#      SD-card payload from the results.
#
# NextUI's own top-level `makefile` is NOT used. Two reasons, both forced:
#   - it hardcodes PLATFORMS and has `tg5040|tg5050` conditionals in `system`,
#     so there is nowhere to add an `alpine` branch without editing it; and
#   - `makefile.toolchain` pulls ghcr.io/loveretro/alpine-toolchain, which does
#     not exist upstream.
#
# NextUI's *component* makefiles under workspace/all/ ARE used, unchanged, as
# leaf compilers. They take CROSS_COMPILE/PREFIX/PLATFORM as inputs and already
# resolve `../../$(PLATFORM)/platform/` correctly for PLATFORM=alpine.
#
# Toolchain: there is no cross-toolchain image. Like Minime, we build the payload
# NATIVELY inside the aarch64 musl builder container, so PREFIX=/usr is the
# container's own sysroot (Alpine's sdl2-dev, mesa-dev, ...) and CROSS_COMPILE is
# just ccache. On an arm64 runner that is native; elsewhere podman emulates it.
# This is also why the target is musl + panfrost: it makes the toolchain a
# non-problem, because the container already *is* the target.

ALPINE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WORKSPACE_DIR := $(abspath $(ALPINE_DIR)/..)
NEXTUI_ROOT := $(abspath $(WORKSPACE_DIR)/..)
PLATFORM := alpine

PODMAN_CMD ?= $(shell command -v podman 2>/dev/null || command -v docker 2>/dev/null || echo podman)
PODMAN_TTY := $(shell [ -t 0 ] && echo "-it" || echo "-i")

SUPPORTED_BOARDS := h700 rk3566
BOARD ?= h700

# The builder image. Overridable so a fork can build its own.
#
# Deliberately NOT registry-qualified. A ghcr.io/... name that is not present
# locally makes the container engine try to pull it, and a build then fails with
# an opaque 403 from the registry instead of the real cause. A bare local name
# cannot be pulled, so a missing image is an obvious local error instead.
BUILDER_IMAGE ?= nextui-alpine-builder:local

# On an arm64 host (GitHub's ubuntu-24.04-arm runner) passing --platform
# linux/arm64 makes crun reject the image with "unknown version specified".
# The host is already arm64, so omit the flag and run the image natively. On
# any other host keep --platform so the arm64 image still runs (emulated).
HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
PLATFORM_ARGS :=
else ifeq ($(HOST_ARCH),arm64)
PLATFORM_ARGS :=
else
PLATFORM_ARGS := --platform linux/arm64
endif

# The repo is mounted at /nextui, not /workspace: the repo itself contains a
# workspace/ directory and mounting over it would hide it.
MOUNTS := --ulimit nofile=65536:65536 -e HOME=/home/builder \
	-v $(NEXTUI_ROOT):/nextui \
	-v $(HOME)/alpine-output/$(BOARD):/alpine-output \
	-v $(HOME)/alpine-ccache:/alpine-ccache \
	-v $(HOME)/alpine-dl:/alpine-dl

# In-container environment. Mirrors Minime's packages/ui/build.sh recipe.
# ccache in front of the compilers keeps rebuilds cheap.
#
# These are passed as podman -e arguments rather than inside the `sh -c` string:
# CROSS_COMPILE="ccache " carries a trailing space, and embedding that in a
# double-quoted sh -c command silently truncates the variable.
#   CROSS_COMPILE="ccache " CC="ccache gcc" sh -c "..."   <- broken
#   run -e CROSS_COMPILE="ccache " -e CC="ccache gcc"     <- correct
CONTAINER_ENV := -e 'CROSS_COMPILE=ccache ' -e 'CC=ccache gcc' -e 'CXX=ccache g++' \
	-e PREFIX=/usr -e PREFIX_LOCAL=/opt/nextui \
	-e UNION_PLATFORM=$(PLATFORM) \
	-e ARCH=aarch64 \
	-e SDKROOT=/ \
	-e BOARD=$(BOARD)

# Runs a shell command inside the builder container.
# Usage: $(call in_container,<shell command>)
define in_container
	@mkdir -p $(HOME)/alpine-output/$(BOARD) $(HOME)/alpine-ccache $(HOME)/alpine-dl/src
	@$(PODMAN_CMD) run $(PODMAN_TTY) --rm --privileged --user 0:0 $(PLATFORM_ARGS) \
		$(MOUNTS) $(CONTAINER_ENV) $(BUILDER_IMAGE) sh -c "$(1)"
endef

.PHONY: help payload payload-components system package components image builder clean

help:
	@printf '%s\n' \
		"NextUI Alpine targets (normally invoked as: task <name>):" \
		"" \
		"  payload     Compile the NextUI payload for the alpine platform" \
		"  system      Assemble the payload into build/ (the makefile.copy step)" \
		"  package     Zip build/ into releases/*.zip" \
		"  components  Build the kernel and rootfs for BOARD" \
		"  image       Package the flashable .img.zst for BOARD" \
		"  builder     Build the builder image locally" \
		"  clean       Remove built outputs"

# ── Payload ─────────────────────────────────────────────────────────────────
#
# The component list is ours, not NextUI's workspace/makefile. NextUI's list is
# ~20 components including TrimUI-specific ones (ledcontrol, bootlogo, show2,
# rfkill, btmanager, poweroff_next) that do not exist for this target. We build
# what the alpine payload actually needs. This mirrors MinUI's shorter list.

PAYLOAD_COMPONENTS := all/nextui all/minarch keymon libmsettings show

# The payload chain is ONE container invocation on purpose:
#   - libmsettings/makefile hard-fails without CROSS_COMPILE, which only exists
#     in the container env, so the compile steps cannot run on the host;
#   - chaining them in a single run keeps every build artifact owned by the same
#     uid, instead of passing root-owned files between separate container runs.
# The individual targets stay callable inside the container for debugging.
payload:
	$(call in_container,cd /nextui/workspace/alpine && \
		$(MAKE) payload-components PLATFORM=$(PLATFORM) && \
		$(MAKE) system PLATFORM=$(PLATFORM) && \
		$(MAKE) package PLATFORM=$(PLATFORM))

# Compile the components, in dependency order. Each is a NextUI component
# makefile driven with PLATFORM=alpine; they resolve `../../$(PLATFORM)/platform/`
# themselves, so nothing here hardcodes a platform path.
#
# Order matters: libmsettings first (platform.c and minarch both link
# -lmsettings), then nextui/minarch, then the per-platform bits.
#
# NOTE: runs inside the container. CROSS_COMPILE / PREFIX / UNION_PLATFORM come
# from CONTAINER_ENV via -e.
payload-components:
	@echo "==> libmsettings"
	$(MAKE) -C $(WORKSPACE_DIR)/alpine/libmsettings PLATFORM=$(PLATFORM)
	@echo "==> nextui"
	$(MAKE) -C $(WORKSPACE_DIR)/all/nextui PLATFORM=$(PLATFORM)
	@echo "==> minarch"
	$(MAKE) -C $(WORKSPACE_DIR)/all/minarch PLATFORM=$(PLATFORM)
	@echo "==> keymon"
	$(MAKE) -C $(WORKSPACE_DIR)/alpine/keymon PLATFORM=$(PLATFORM)
	@echo "==> show"
	$(MAKE) -C $(WORKSPACE_DIR)/alpine/show PLATFORM=$(PLATFORM)

# Assemble the compiled pieces into build/. This is our platform/makefile.copy,
# replacing the ~50 hardcoded `cp` lines in NextUI's top-level makefile, which
# we cannot extend with an `alpine` branch.
system:
	$(MAKE) -f $(ALPINE_DIR)/platform/makefile.copy PLATFORM=$(PLATFORM) \
		WORKSPACE_DIR=$(WORKSPACE_DIR) ALPINE_DIR=$(ALPINE_DIR) \
		NEXTUI_ROOT=$(NEXTUI_ROOT)

# Zip build/ into releases/ for the image stage. build/ is seeded from the
# NextUI skeleton first, so the payload has the BASE/BIOS/Roms/Saves tree the
# device expects.
package:
	@mkdir -p $(NEXTUI_ROOT)/releases
	@rm -rf $(WORKSPACE_DIR)/build
	@mkdir -p $(WORKSPACE_DIR)/build
	@cp -R $(NEXTUI_ROOT)/skeleton/. $(WORKSPACE_DIR)/build/
	@find $(WORKSPACE_DIR)/build -type f -name '.keep' -delete
	@$(MAKE) --no-print-directory -C $(ALPINE_DIR) system PLATFORM=$(PLATFORM)
	@release="$$(cd $(NEXTUI_ROOT) && make --no-print-directory name)"; \
	cd $(WORKSPACE_DIR)/build && zip -qr $(NEXTUI_ROOT)/releases/$$release-base.zip \
		BASE SYSTEM EXTRAS; \
	echo "packaged: releases/$$release-base.zip"

# ── OS image ────────────────────────────────────────────────────────────────

components:
	$(call in_container,BOARD=$(BOARD) ALPINE_DIR=/nextui/workspace/alpine NEXTUI_ROOT=/nextui \
		/nextui/workspace/alpine/scripts/build.sh components)

image:
	$(call in_container,/nextui/workspace/alpine/image/build.sh \
		--board $(BOARD) \
		--input-dir /nextui/workspace/alpine/out/$(BOARD) \
		--output-dir /nextui/workspace/alpine/out/$(BOARD) \
		--payload-dir /nextui/releases \
		--nextui-release "$(NEXTUI_RELEASE)")

# Build with the same engine that `in_container` runs with. Hardcoding docker
# here put the image in docker's store while PODMAN_CMD preferred podman, so the
# run found nothing and tried to pull the image from a registry instead.
builder:
	$(PODMAN_CMD) build -t $(BUILDER_IMAGE) $(PLATFORM_ARGS) $(ALPINE_DIR)/container

clean:
	rm -rf $(WORKSPACE_DIR)/build $(NEXTUI_ROOT)/releases
	rm -rf $(ALPINE_DIR)/out/$(BOARD)
