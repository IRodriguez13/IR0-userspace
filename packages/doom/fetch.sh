#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Doom sources live in the IR0 tree (setup/doom/). No tarball fetch.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IR0_ROOT="${IR0_ROOT:-${ROOT}/../IR0}"
PORT="${IR0_ROOT}/setup/doom/doomgeneric_ir0.c"
UPSTREAM="${IR0_ROOT}/setup/doom/upstream/doomgeneric"

if [ ! -f "$PORT" ] || [ ! -d "$UPSTREAM" ]; then
	echo "✗ doom: missing IR0 port at $PORT (set IR0_ROOT=…)" >&2
	exit 1
fi

echo "  FETCH   doom from IR0_ROOT=${IR0_ROOT} (setup/doom — no tarball)"
echo "✓ fetch doom OK"
