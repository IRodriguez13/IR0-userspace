#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

TREE="${1:?}"
OUT="${2:?}"
{
	echo "# path type mode uid gid size sha256_or_target package"
	find "$TREE" -print0 | sort -z | while IFS= read -r -d '' p; do
		rel="/${p#"${TREE}/"}"
		[ "$rel" = "/" ] && continue
		if [ -L "$p" ]; then
			printf '%s symlink %s %s %s 0 %s -\n' \
				"$rel" "$(stat -c '%a' "$p")" "$(stat -c '%u' "$p")" \
				"$(stat -c '%g' "$p")" "$(readlink "$p")"
		elif [ -d "$p" ]; then
			printf '%s dir %s %s %s 0 - -\n' \
				"$rel" "$(stat -c '%a' "$p")" "$(stat -c '%u' "$p")" \
				"$(stat -c '%g' "$p")"
		elif [ -f "$p" ]; then
			printf '%s file %s %s %s %s %s -\n' \
				"$rel" "$(stat -c '%a' "$p")" "$(stat -c '%u' "$p")" \
				"$(stat -c '%g' "$p")" "$(stat -c '%s' "$p")" \
				"$(sha256sum "$p" | awk '{print $1}')"
		fi
	done
} > "$OUT"
echo "✓ rootfs-manifest $OUT"
