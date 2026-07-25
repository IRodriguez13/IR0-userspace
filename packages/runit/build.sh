#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build static runit tools (PID 1 + supervision) for IR0 userspace.
#
# Source: https://smarden.org/runit/
# Void template reference: void-packages srcpkgs/runit/template

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="${ROOT}/packages/runit"
SRC_DIR="${PKG}/src"
OUT_DIR="${ROOT}/out/bin"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi
if [ ! -d "$SRC_DIR/src" ]; then
	echo "✗ missing runit source; run: make fetch" >&2
	exit 1
fi

mkdir -p "$OUT_DIR"

echo "  RUNIT   Building with $CC (static)..."
cd "$SRC_DIR/src"

echo "$CC -D_GNU_SOURCE -static -Os -fno-pie" >conf-cc
echo "$CC -static -no-pie" >conf-ld

make -s clean 2>/dev/null || true
make -s sysdeps
make -s runit runit-init runsvdir runsv sv chpst

for bin in runit runit-init runsvdir runsv sv chpst; do
	install -m 0755 "$bin" "$OUT_DIR/$bin"
	file "$OUT_DIR/$bin" | grep -q ELF
done

echo "✓ build runit OK ($(ls -1 "$OUT_DIR" | tr '\n' ' '))"
