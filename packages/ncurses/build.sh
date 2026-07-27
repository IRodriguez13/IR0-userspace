#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build static ncurses (narrowc) with linux/vt100 fallbacks for IR0 nano.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/toolchain.sh"
PKG="${ROOT}/packages/ncurses"
SRC="${PKG}/src"
PREFIX="${PKG}/prefix/${ARCH}"
if [ ! -d "$SRC" ]; then
	echo "✗ missing ncurses source; run: make fetch" >&2
	exit 1
fi

if [ -f "${PREFIX}/lib/libncurses.a" ] && [ -f "${PREFIX}/include/ncurses.h" ]; then
	echo "✓ ncurses already installed → ${PREFIX}"
	exit 0
fi

mkdir -p "$PREFIX"
cd "$SRC"

echo "  NCURSES Configuring $(cat "$PKG/version") (static, fallbacks)..."
make distclean >/dev/null 2>&1 || true
cfg_log="${PKG}/configure-${ARCH}.log"
if ! CC="$CC" CFLAGS="-Os -fno-pie" LDFLAGS="-static -no-pie" \
	./configure \
	--prefix=/usr \
	--host="${TARGET_TRIPLE}" \
	--with-build-cc=gcc \
	--without-shared \
	--with-normal \
	--without-debug \
	--without-ada \
	--without-cxx \
	--without-cxx-binding \
	--without-progs \
	--without-tests \
	--disable-db-install \
	--without-manpages \
	--with-fallbacks=linux,vt100,xterm \
	--disable-widec \
	--enable-termcap \
	>"${cfg_log}" 2>&1; then
	cat "${cfg_log}" >&2
	exit 1
fi

echo "  NCURSES Building..."
make -s -j"$(nproc)"
make -s DESTDIR="${PREFIX}" install.libs install.includes
# DESTDIR+/usr → flatten for consumers
if [ -d "${PREFIX}/usr" ]; then
	mkdir -p "${PREFIX}/lib" "${PREFIX}/include"
	cp -a "${PREFIX}/usr/lib/." "${PREFIX}/lib/" 2>/dev/null || true
	cp -a "${PREFIX}/usr/include/." "${PREFIX}/include/" 2>/dev/null || true
fi

test -f "${PREFIX}/lib/libncurses.a" || test -f "${PREFIX}/usr/lib/libncurses.a"
echo "✓ build ncurses OK → ${PREFIX}"
