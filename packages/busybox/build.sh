#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build the two IR0 BusyBox binaries from one source tree:
#   out/busybox-full  0755  general applets, never privileged
#   out/busybox-auth  4755  login + su only (installed set-user-ID root)
#
# Both builds share packages/busybox/src, so they run sequentially under a lock.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="${ROOT}/packages/busybox"
SRC="${PKG}/src"
OUT_DIR="${ROOT}/out"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi
if [ ! -f "$SRC/Makefile" ]; then
	echo "✗ missing BusyBox source; run: make fetch" >&2
	exit 1
fi

mkdir -p "$OUT_DIR"
chmod +x "${ROOT}/scripts/busybox_apply_fragment.sh"

build_variant()
{
	fragment="$1"
	out="$2"

	echo "  BUSYBOX Building $(basename "$out") from $(basename "$fragment")"
	"${ROOT}/scripts/busybox_apply_fragment.sh" "$SRC" "$fragment"
	flock /tmp/ir0-busybox-src.lock \
		make -C "$SRC" CC="$CC" CFLAGS="-fno-pie" LDFLAGS="-no-pie" \
		-s -j"$(nproc)"
	cp -f "$SRC/busybox" "$out"
	file "$out" | grep -q ELF
}

build_variant "${PKG}/ir0_full.config" "${OUT_DIR}/busybox-full"
"${OUT_DIR}/busybox-full" --list | grep -qx sh
if "${OUT_DIR}/busybox-full" --list | grep -qxE 'login|su|passwd'; then
	echo "✗ privileged applet inside the general binary" >&2
	exit 1
fi
echo "✓ busybox-full OK ($("${OUT_DIR}/busybox-full" --list | wc -l) applets)"

build_variant "${PKG}/ir0_auth.config" "${OUT_DIR}/busybox-auth"
"${OUT_DIR}/busybox-auth" --list | sort > /tmp/ir0-bb-auth-applets.txt
printf 'login\nsu\n' | sort | diff -u - /tmp/ir0-bb-auth-applets.txt >/dev/null || {
	echo "✗ auth binary applet set drifted (expected login + su only)" >&2
	exit 1
}
echo "✓ busybox-auth OK (login, su)"
