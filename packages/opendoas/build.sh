#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build OpenDoas portable (static musl) for IR0 userspace.
#
# Upstream: https://github.com/Duncaen/OpenDoas
# Auth: shadow(5); persist: --with-timestamp → /run/doas tickets (needs
# /proc/[pid]/stat, getsid(2) and futimens(2) on the kernel side).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="${ROOT}/packages/opendoas"
SRC="${PKG}/src"
OUT_DIR="${ROOT}/out/stage-bin"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi
if [ ! -d "$SRC" ]; then
	echo "✗ missing OpenDoas source; run: make fetch" >&2
	exit 1
fi

mkdir -p "$OUT_DIR"
cd "$SRC"

echo "  DOAS    Configuring OpenDoas $(cat "$PKG/version") (shadow + timestamp, static)..."
# musl provides crypt(3) in libc — do not ask the linker for -lcrypt.
CC="$CC" CFLAGS="-static -Os -fno-pie" LDFLAGS="-static -no-pie" \
	./configure --prefix=/usr --sysconfdir=/etc \
	--without-pam --with-timestamp --enable-static >/tmp/ir0-opendoas-configure.log
if grep -q -- '-lcrypt' config.mk; then
	sed -i 's/LDLIBS +=	-lcrypt/LDLIBS +=/' config.mk
fi

echo "  DOAS    Building..."
make clean >/dev/null 2>&1 || true
make -s CC="$CC" CFLAGS="-static -Os -fno-pie" LDFLAGS="-static -no-pie"
install -m 0755 doas "$OUT_DIR/doas"
file "$OUT_DIR/doas" | grep -q ELF
echo "✓ build opendoas OK → $OUT_DIR/doas"
