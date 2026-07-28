#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Maintainer-only: rebuild packages/busybox/*_defconfig from a fragment.
# Bounded, no script(1). Run manually after editing *.config fragments.
#
# If the fragment contains a line `# IR0_BASE=defconfig`, start from upstream
# `make defconfig` then apply the overlay (y/n/value). Otherwise start from
# `allnoconfig` (legacy small fragments).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUSYBOX_SRC="${BUSYBOX_SRC:-$ROOT/packages/busybox/src}"
FRAG="${1:-$ROOT/packages/busybox/fase58_busybox.config}"
OLDCONFIG_TIMEOUT="${OLDCONFIG_TIMEOUT:-180}"

if [[ ! -f "$FRAG" ]]; then
	echo "usage: $0 [CONFIG_FRAGMENT]" >&2
	exit 2
fi
if [[ ! -d "$BUSYBOX_SRC" ]]; then
	echo "missing BUSYBOX_SRC=$BUSYBOX_SRC" >&2
	exit 1
fi

FRAG_ABS="$(cd "$(dirname "$FRAG")" && pwd)/$(basename "$FRAG")"
OUT="${FRAG_ABS%.config}_defconfig"
BASE="allnoconfig"
if grep -qE '^[[:space:]]*#[[:space:]]*IR0_BASE=defconfig[[:space:]]*$' "$FRAG_ABS"; then
	BASE="defconfig"
fi

echo "  BUSYBOX regenerate $OUT from $(basename "$FRAG_ABS") (base=$BASE)"

make -C "$BUSYBOX_SRC" distclean </dev/null >/dev/null 2>&1 || true
make -C "$BUSYBOX_SRC" "$BASE" </dev/null >/dev/null 2>&1
CFG="${BUSYBOX_SRC}/.config"

while IFS= read -r line; do
	case "$line" in
	""|\#*) continue ;;
	esac
	sym="${line%%=*}"
	val="${line#*=}"
	case "$val" in
	y)
		sed -i "s/^# ${sym} is not set/${sym}=y/" "$CFG"
		if ! grep -q "^${sym}=y\$" "$CFG"; then
			if grep -q "^${sym}=" "$CFG"; then
				sed -i "s/^${sym}=.*/${sym}=y/" "$CFG"
			else
				echo "${sym}=y" >> "$CFG"
			fi
		fi
		;;
	n)
		sed -i "s/^${sym}=.*/# ${sym} is not set/" "$CFG"
		if ! grep -q "^# ${sym} is not set\$" "$CFG"; then
			echo "# ${sym} is not set" >> "$CFG"
		fi
		# Also clear any =y left behind
		sed -i "s/^${sym}=y\$/# ${sym} is not set/" "$CFG"
		;;
	*)
		if grep -q "^${sym}=" "$CFG"; then
			sed -i "s|^${sym}=.*|${sym}=${val}|" "$CFG"
		elif grep -q "^# ${sym} is not set\$" "$CFG"; then
			sed -i "s|^# ${sym} is not set\$|${sym}=${val}|" "$CFG"
		else
			echo "${sym}=${val}" >> "$CFG"
		fi
		;;
	esac
done < "$FRAG_ABS"

timeout "$OLDCONFIG_TIMEOUT" bash -c 'yes "" | make -C "'"$BUSYBOX_SRC"'" oldconfig' \
	</dev/null >/dev/null 2>&1

cp -f "$CFG" "$OUT"
echo "✓ wrote $OUT ($(wc -l < "$OUT") lines)"
