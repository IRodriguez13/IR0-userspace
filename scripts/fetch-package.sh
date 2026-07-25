#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fetch and unpack one package from packages/<name>/{url,version,sha256,srcroot}.
# Downloads land in packages/<name>/dist and the verified tree in
# packages/<name>/src. Already-unpacked trees are left untouched so `make build`
# works offline.

set -euo pipefail

NAME="${1:?usage: fetch-package.sh <package>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="${ROOT}/packages/${NAME}"

[ -d "$PKG" ] || { echo "✗ unknown package: $NAME" >&2; exit 1; }

URL="$(cat "$PKG/url")"
SRCROOT="$(cat "$PKG/srcroot")"
TARBALL="$(basename "$URL")"
# Upstream file names do not always match the archive URL (GitHub tag tarballs).
CHECKED_NAME="$(awk '{print $2}' "$PKG/sha256")"
[ -n "$CHECKED_NAME" ] && TARBALL="$CHECKED_NAME"

mkdir -p "$PKG/dist"

if [ ! -f "$PKG/dist/$TARBALL" ]; then
	echo "  FETCH   $NAME → $TARBALL"
	curl -fsSL "$URL" -o "$PKG/dist/$TARBALL"
fi

( cd "$PKG/dist" && sha256sum -c "$PKG/sha256" >/dev/null )
echo "  FETCH   $NAME checksum OK"

if [ -d "$PKG/src" ]; then
	echo "  FETCH   $NAME already unpacked (packages/$NAME/src)"
	exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
tar -xf "$PKG/dist/$TARBALL" -C "$TMP"
[ -d "$TMP/$SRCROOT" ] || { echo "✗ $NAME: missing $SRCROOT in archive" >&2; exit 1; }
mv "$TMP/$SRCROOT" "$PKG/src"

shopt -s nullglob
for p in "$PKG"/patches/*.patch; do
	echo "  PATCH   $NAME $(basename "$p")"
	patch -p1 -d "$PKG/src" -i "$p" --no-backup-if-mismatch
done

echo "✓ fetch $NAME OK"
