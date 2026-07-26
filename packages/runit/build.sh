#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/toolchain.sh"
PKG="${ROOT}/packages/runit"
SRC_DIR="${PKG}/src"
OUT_DIR="${PRODUCT_OUT:-${ROOT}/out/${ARCH}/product}/bin"

if [ ! -d "$SRC_DIR/src" ]; then
	echo "✗ missing runit source; run: make fetch" >&2
	exit 1
fi

mkdir -p "$OUT_DIR"
echo "  RUNIT   Building with $CC (static) ARCH=${ARCH}..."
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
