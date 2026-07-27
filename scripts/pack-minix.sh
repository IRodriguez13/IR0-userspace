#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# MINIX adapter: pack a finished rootfs tree into disk.img via IR0 inject tooling.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TREE="${1:?usage: pack-minix.sh ROOTFS_TREE DISK}"
DISK="${2:?usage: pack-minix.sh ROOTFS_TREE DISK}"
IR0_ROOT="${IR0_ROOT:-${ROOT}/../IR0}"
INJECT="python3 ${IR0_ROOT}/scripts/inject_init_minix.py"
PROFILE="${PROFILE:-${IR0_PRODUCT_PROFILE:-minimal}}"

if [ ! -f "${IR0_ROOT}/scripts/inject_init_minix.py" ]; then
	echo "✗ set IR0_ROOT for MINIX packing" >&2
	exit 1
fi
if [ ! -d "$TREE" ]; then
	echo "✗ missing rootfs tree: $TREE" >&2
	exit 1
fi
if [ ! -f "$DISK" ]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi

inject_file()
{
	local src="$1" dest="$2"
	shift 2
	local mode
	mode=$(stat -c '%a' "$src")
	case "$mode" in
	4*|2*|6*)
		$INJECT "$DISK" --setuid "$@" "$src" "$dest"
		;;
	*)
		$INJECT "$DISK" --mode "$mode" "$@" "$src" "$dest"
		;;
	esac
}

echo "  MINIX   packing tree $TREE → $DISK"

# Empty dirs first (pseudo-fs mountpoints + firstboot state). MINIX inject
# creates parents when writing a file; use .keep placeholders.
for d in var/lib/ir0 var/log tmp dev proc sys heart run run/doas mnt; do
	mkdir -p "${TREE}/${d}"
	touch "${TREE}/${d}/.keep"
	$INJECT "$DISK" --mode 0644 "${TREE}/${d}/.keep" "${d}/.keep"
done

inject_file "${TREE}/sbin/init" sbin/init
inject_file "${TREE}/sbin/runit" sbin/runit
inject_file "${TREE}/bin/runit-init" bin/runit-init
inject_file "${TREE}/bin/runsvdir" bin/runsvdir
inject_file "${TREE}/bin/runsv" bin/runsv
inject_file "${TREE}/bin/sv" bin/sv
inject_file "${TREE}/sbin/fsck.ir0" sbin/fsck.ir0
inject_file "${TREE}/sbin/ir0-firstboot" sbin/ir0-firstboot
inject_file "${TREE}/sbin/ir0-recovery" sbin/ir0-recovery
inject_file "${TREE}/sbin/mount-root-rw" sbin/mount-root-rw
inject_file "${TREE}/bin/passwd" bin/passwd
if [ -f "${TREE}/usr/sbin/adduser" ]; then
	inject_file "${TREE}/usr/sbin/adduser" usr/sbin/adduser
	$INJECT --hardlink "$DISK" usr/sbin/adduser sbin/adduser
fi
inject_file "${TREE}/bin/ir0-status" bin/ir0-status
inject_file "${TREE}/usr/bin/busybox-auth" usr/bin/busybox-auth
$INJECT --hardlink "$DISK" usr/bin/busybox-auth bin/login
$INJECT --hardlink "$DISK" usr/bin/busybox-auth bin/su

if [ -f "${TREE}/usr/bin/doas" ]; then
	inject_file "${TREE}/usr/bin/doas" usr/bin/doas
fi
if [ -f "${TREE}/usr/bin/nano" ]; then
	inject_file "${TREE}/usr/bin/nano" usr/bin/nano
fi
if [ -f "${TREE}/etc/doas.conf" ]; then
	$INJECT "$DISK" --mode 0440 "${TREE}/etc/doas.conf" etc/doas.conf
fi

BUSYBOX="${TREE}/bin/busybox"
MANIFEST="${ROOT}/profiles/${PROFILE}/applets.txt"
[ -f "$MANIFEST" ] || MANIFEST="${ROOT}/packages/busybox/required_applets.txt"
chmod +x "${ROOT}/scripts/busybox_inject_manifest.sh"
IR0_ROOT="$IR0_ROOT" FASE50_BUSYBOX_BIN="$BUSYBOX" \
	"${ROOT}/scripts/busybox_inject_manifest.sh" "$DISK" "$BUSYBOX" "$MANIFEST"

inject_file "${TREE}/etc/runit/1" etc/runit/1
inject_file "${TREE}/etc/runit/2" etc/runit/2
inject_file "${TREE}/etc/runit/3" etc/runit/3
inject_file "${TREE}/etc/runit/sv/console/run" etc/runit/sv/console/run
inject_file "${TREE}/etc/runit/sv/logger/run" etc/runit/sv/logger/run

for f in passwd group issue hostname profile os-release shells hosts \
	console.conf ir0-profile resolv.conf man.conf; do
	[ -f "${TREE}/etc/${f}" ] || continue
	mode=0644
	[ "$f" = "shadow" ] && continue
	$INJECT "$DISK" --mode "$mode" "${TREE}/etc/${f}" "etc/${f}"
done
$INJECT "$DISK" --mode 0600 "${TREE}/etc/shadow" etc/shadow
[ -f "${TREE}/etc/ir0-noroot" ] && \
	$INJECT "$DISK" --mode 0644 "${TREE}/etc/ir0-noroot" etc/ir0-noroot
[ -f "${TREE}/etc/ir0-autologin" ] && \
	$INJECT "$DISK" --mode 0644 "${TREE}/etc/ir0-autologin" etc/ir0-autologin
[ -f "${TREE}/etc/busybox/bb_status.tsv" ] && \
	$INJECT "$DISK" --mode 0644 "${TREE}/etc/busybox/bb_status.tsv" etc/busybox/bb_status.tsv
[ -f "${TREE}/etc/network/interfaces" ] && \
	$INJECT "$DISK" --mode 0644 "${TREE}/etc/network/interfaces" etc/network/interfaces
[ -f "${TREE}/usr/lib/ir0/build-info" ] && \
	$INJECT "$DISK" --mode 0644 "${TREE}/usr/lib/ir0/build-info" usr/lib/ir0/build-info

# Guest man pages (pre-rendered ASCII cat7)
if [ -d "${TREE}/usr/share/man/cat7" ]; then
	for page in "${TREE}/usr/share/man/cat7"/IR0-*.7; do
		[ -f "$page" ] || continue
		base="$(basename "$page")"
		$INJECT "$DISK" --mode 0644 "$page" "usr/share/man/cat7/${base}"
	done
fi

# Homes
if [ -d "${TREE}/root" ]; then
	touch "${TREE}/root/.keep"
	$INJECT "$DISK" "${TREE}/root/.keep" root/.keep
	$INJECT --owner 0:0 --mode 0700 --chown "$DISK" root 2>/dev/null || true
fi
if [ -d "${TREE}/home/labuser" ]; then
	touch "${TREE}/home/labuser/.keep"
	$INJECT "$DISK" --mode 0644 --owner 1000:100 \
		"${TREE}/home/labuser/.keep" home/labuser/.keep
	$INJECT --owner 1000:100 --mode 0700 --chown "$DISK" home/labuser
fi

VERIFY_EXTRA=()
[ -f "${TREE}/usr/bin/nano" ] && VERIFY_EXTRA+=(/usr/bin/nano)
[ -f "${TREE}/usr/bin/doas" ] && VERIFY_EXTRA+=(/usr/bin/doas)

python3 "${IR0_ROOT}/scripts/verify_minix_rootfs.py" --gate "$DISK" \
	/sbin/init /sbin/runit /bin/runsvdir /bin/sh /bin/busybox \
	/sbin/fsck.ir0 /sbin/ir0-firstboot /sbin/ir0-recovery /sbin/mount-root-rw /bin/passwd \
	/usr/bin/busybox-auth /bin/login /bin/su \
	/etc/passwd /etc/shadow /etc/group /etc/os-release \
	/etc/runit/1 /etc/runit/2 /etc/runit/3 \
	/etc/runit/sv/console/run /etc/runit/sv/logger/run \
	"${VERIFY_EXTRA[@]}"

echo "  MINIX   packed $DISK"
