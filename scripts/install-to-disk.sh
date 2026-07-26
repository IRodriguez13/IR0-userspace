#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compatibility wrapper: stage rootfs tree, then pack into MINIX disk.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:?usage: install-to-disk.sh DISK_IMAGE}"
PROFILE="${IR0_PRODUCT_PROFILE:-minimal}"
ARCH="${ARCH:-x86_64}"

chmod +x "${ROOT}/scripts/stage-rootfs.sh" "${ROOT}/scripts/pack-minix.sh" \
	"${ROOT}/scripts/toolchain.sh"
# shellcheck disable=SC1091
source "${ROOT}/scripts/toolchain.sh"

TREE="${ROOTFS_OUT}/${PROFILE}"
IR0_ROOT="${IR0_ROOT}" IR0_PRODUCT_PROFILE="$PROFILE" ARCH="$ARCH" \
	PRODUCT_OUT="$PRODUCT_OUT" ROOTFS_OUT="$ROOTFS_OUT" \
	IR0_GUEST_MANDOC_DIR="${IR0_GUEST_MANDOC_DIR:-}" \
	"${ROOT}/scripts/stage-rootfs.sh" "$TREE"

IR0_ROOT="${IR0_ROOT}" PROFILE="$PROFILE" \
	"${ROOT}/scripts/pack-minix.sh" "$TREE" "$DISK"
