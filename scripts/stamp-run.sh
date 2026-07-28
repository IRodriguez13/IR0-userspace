#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run a command and write STAMP only on success (failure leaves stamp untouched).
#
# Usage: stamp-run.sh STAMP [--] command [args...]
set -euo pipefail

if [ "$#" -lt 2 ]; then
	echo "usage: stamp-run.sh STAMP [--] command [args...]" >&2
	exit 2
fi

stamp="$1"
shift
if [ "${1:-}" = "--" ]; then
	shift
fi
if [ "$#" -lt 1 ]; then
	echo "usage: stamp-run.sh STAMP [--] command [args...]" >&2
	exit 2
fi

mkdir -p "$(dirname "$stamp")"
"$@"
# Only reached on success (set -e).
{
	echo "ok"
	date -u +%Y-%m-%dT%H:%M:%SZ
} >"${stamp}.tmp.$$"
mv -f "${stamp}.tmp.$$" "$stamp"
