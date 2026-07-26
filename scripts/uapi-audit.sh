#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fail if private kernel headers leaked into the UAPI sysroot or package sources
# that we compile (excluding upstream tarball trees under packages/*/src).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SYS="${IR0_UAPI_SYSROOT:-${ROOT}/sysroot}"
fail=0

if [ ! -d "${SYS}/usr/include" ]; then
	echo "✗ missing ${SYS}/usr/include (make headers)" >&2
	exit 1
fi

# Private trees must not appear as installable UAPI.
for bad in kernel mm arch fs sched drivers; do
	if [ -d "${SYS}/usr/include/${bad}" ] || [ -d "${SYS}/include/${bad}" ]; then
		echo "✗ private header tree in UAPI sysroot: $bad" >&2
		fail=1
	fi
done

# Source scan: services/ and lib/ must not include private paths.
while IFS= read -r hit; do
	echo "✗ private include: $hit" >&2
	fail=1
done < <(grep -RnE '#include[[:space:]]*[<"](kernel|mm|arch|fs|sched|drivers)/' \
	"${ROOT}/services" "${ROOT}/lib" "${ROOT}/smoke" 2>/dev/null || true)

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "✓ uapi-audit OK"
