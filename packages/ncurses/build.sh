#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build static ncurses (narrowc) with linux/vt100 fallbacks for IR0 nano.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="${ROOT}/packages/ncurses"
SRC="${PKG}/src"
PREFIX="${PKG}/prefix"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi
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
CC="$CC" CFLAGS="-Os -fno-pie" LDFLAGS="-static -no-pie" \
	./configure \
	--prefix=/usr \
	--host=x86_64-linux-musl \
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
	>/tmp/ir0-ncurses-configure.log

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
