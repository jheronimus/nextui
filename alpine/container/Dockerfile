# NextUI Alpine builder image.
#
# Provides everything the image build needs that a developer host will not have:
#   - abuild/APKBUILD helpers for building our APKs for aarch64 musl
#   - kernel build deps for tinykernel (bison, flex, bc, perl, python3)
#   - image assembly tools (erofs-utils, dosfstools, mtools, cpio, genimage,
#     u-boot-tools, patchelf)
#   - the NextUI toolchain libs: SDL2 + image/ttf/mixer, Mesa dev (panfrost),
#     libdrm, libsamplerate, bzip2/xz/zstd/lz4 dev
#
# Derived from Minime's packages/components/alpine/container/Dockerfile. Changes:
# their static GPG signing key is NOT copied -- it is Minime's private key and
# belongs to their project. We generate a keypair at image build time instead,
# so the image is not reproducible bit-for-bit but nothing is misappropriated.
# UI build toolchains (rust/cargo, llvm, nodejs, npm, cmake, py3-pip) are
# dropped; we build no Rust or JavaScript and they are the bulk of the image.
#
# The --platform is always linux/arm64: the target is aarch64 and the build
# tools are run natively inside the container.

FROM --platform=linux/arm64 docker.io/library/alpine:3.24.2

# aarch64-native main + community repos only. bsdtar is provided by
# libarchive-tools on Alpine 3.24 (no top-level bsdtar package).
RUN echo "http://dl-cdn.alpinelinux.org/alpine/v3.24/main"      > /etc/apk/repositories && \
	echo "http://dl-cdn.alpinelinux.org/alpine/v3.24/community" >> /etc/apk/repositories

# Each package on its own line so apk names the exact missing one on failure.
# musl-dev below is the native aarch64 musl headers, and binutils/gcc are the
# native aarch64 tools. There is no gcc-aarch64-alpine, binutils-aarch64-alpine
# or musl-dev-aarch64 in Alpine -- those names exist in no repo. This builder
# runs ON aarch64 and compiles natively, so the plain native packages are the
# right ones and a cross-toolchain is not wanted at all. check-packages.sh
# verifies every name here against the real APKINDEX.
RUN apk add --no-cache --no-progress \
		abuild \
		alpine-sdk \
		bash \
		bc \
		bison \
		ccache \
		cpio \
		curl \
		dtc \
		file \
		flex \
		genimage \
		git \
		libarchive-tools \
		linux-headers \
		m4 \
		mtools \
		musl-dev \
		ncurses-dev \
		openssl-dev \
		pahole \
		perl \
		pkgconf \
		python3 \
		python3-dev \
		rsync \
		sudo \
		u-boot-tools \
		patchelf \
		wget \
		# image assembly
		bzip2 \
		dosfstools \
		erofs-utils \
		squashfs-tools \
		util-linux \
		# cross toolchain used by abuild for aarch64 targets
		binutils \
		gcc \
		# NextUI payload toolchain (mirrors workspace/*/makefile LDFLAGS)
		alsa-lib-dev \
		tinyalsa-dev \
		bzip2-dev \
		g++ \
		libdrm-dev \
		libsamplerate-dev \
		mesa-dev \
		sdl2-dev \
		sdl2_image-dev \
		sdl2_mixer-dev \
		sdl2_ttf-dev \
		xz-dev \
		zlib-dev \
		zstd-dev \
		lz4-dev \
		openssl-dev \
		freetype-dev \
		libpng-dev \
		gmp-dev \
		mpfr-dev \
		mpc1-dev \
	&& true

# abuild requires a non-root "packager" account. Match the host UID/GID passed
# by --user so abuild-built files are owned by the host user.
ARG BUILDER_UID=1000
ARG BUILDER_GID=1000
RUN addgroup -g ${BUILDER_GID} builder 2>/dev/null || true \
	&& adduser -u ${BUILDER_UID} -G builder -D -h /home/builder -s /bin/sh builder \
	&& addgroup builder abuild 2>/dev/null || true \
	&& mkdir -p /etc/sudoers.d \
	&& echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/builder \
	&& chmod 0440 /etc/sudoers.d/builder

# abuild signing key. Generated here rather than copied: this is OUR key, not a
# vendored one. Losing it only means locally-built APKs are unsigned, which the
# rootfs install already tolerates via --allow-untrusted.
#
# abuild-keygen has no option to choose the key name: it always names the key
# "${USER}-$(date +%s)". So the path cannot be hardcoded here. Instead let the
# tool do its job -- -a generates the key and records PACKAGER_PRIVKEY in
# abuild.conf, -i installs the public key into /etc/apk/keys -- and only append
# our own paths afterwards, with >> so PACKAGER_PRIVKEY survives.
RUN mkdir -p /home/builder/.abuild /etc/apk/keys \
	&& chown -R builder:builder /home/builder/.abuild \
	&& su builder -c '/usr/bin/abuild-keygen -a -i -n' \
	&& chmod 700 /home/builder/.abuild \
	&& printf '%s\n' \
		'REPODEST="/alpine-output/packages/main"' \
		'SRCDEST="/alpine-dl/src"' \
		'BUILDDIR="/alpine-output/build"' \
		>> /home/builder/.abuild/abuild.conf

# Cache + output directories the Makefile mounts from the host.
RUN mkdir -p /alpine-output /alpine-ccache /alpine-dl/src \
	&& chown -R builder:builder /alpine-output /alpine-ccache /alpine-dl

ENV CCACHE_DIR=/alpine-ccache
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

USER builder
WORKDIR /workspace
