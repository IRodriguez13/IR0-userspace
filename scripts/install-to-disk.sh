#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Inject runit rootfs layout + binaries into a MINIX disk image.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:?usage: install-to-disk.sh DISK_IMAGE}"
# MINIX image tooling lives in the kernel tree; everything else is userspace.
IR0_ROOT="${IR0_ROOT:-$(cd "${ROOT}/../IR0" 2>/dev/null && pwd || true)}"
if [ ! -f "${IR0_ROOT}/scripts/inject_init_minix.py" ]; then
	echo "✗ kernel tree not found; set IR0_ROOT=/path/to/IR0" >&2
	exit 1
fi
RUNIT_BIN="${ROOT}/out/bin"
STAGE_BIN="${ROOT}/out/stage-bin"
BUSYBOX="${IR0_BUSYBOX_FULL_BIN:-${ROOT}/out/busybox-full}"
BUSYBOX_AUTH="${IR0_BUSYBOX_AUTH_BIN:-${ROOT}/out/busybox-auth}"
INJECT="python3 ${IR0_ROOT}/scripts/inject_init_minix.py"
MANIFEST="${BUSYBOX_MANIFEST:-${ROOT}/packages/busybox/required_applets.txt}"

if [ ! -f "$DISK" ]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi

need_bin() {
	if [ ! -f "$1" ]; then
		echo "✗ missing $1 (run: make build)" >&2
		exit 1
	fi
}

need_bin "$RUNIT_BIN/runit"
need_bin "$RUNIT_BIN/runit-init"
need_bin "$RUNIT_BIN/runsvdir"
need_bin "$RUNIT_BIN/runsv"
need_bin "$STAGE_BIN/runit_stage1"
need_bin "$STAGE_BIN/runit_stage2"
need_bin "$STAGE_BIN/runit_stage3"
need_bin "$STAGE_BIN/runit_console_run"
need_bin "$STAGE_BIN/runit_logger_run"
need_bin "$STAGE_BIN/fsck.ir0"
need_bin "$STAGE_BIN/ir0_firstboot"
need_bin "$STAGE_BIN/ir0_passwd"
need_bin "$STAGE_BIN/ir0_status"
need_bin "$STAGE_BIN/ir0_recovery"
need_bin "$STAGE_BIN/mount_root_rw"
need_bin "$BUSYBOX"
need_bin "$BUSYBOX_AUTH"

ROOTFS_ETC="${ROOT}/rootfs/etc"

echo "  RUNIT   Injecting binaries..."
$INJECT "$DISK" "$RUNIT_BIN/runit-init" sbin/init
$INJECT "$DISK" "$RUNIT_BIN/runit" sbin/runit
$INJECT "$DISK" "$RUNIT_BIN/runit-init" bin/runit-init
$INJECT "$DISK" "$RUNIT_BIN/runsvdir" bin/runsvdir
$INJECT "$DISK" "$RUNIT_BIN/runsv" bin/runsv
$INJECT "$DISK" "$RUNIT_BIN/sv" bin/sv
$INJECT "$DISK" "$STAGE_BIN/fsck.ir0" sbin/fsck.ir0
$INJECT "$DISK" "$STAGE_BIN/ir0_firstboot" sbin/ir0-firstboot
$INJECT "$DISK" "$STAGE_BIN/ir0_recovery" sbin/ir0-recovery
$INJECT "$DISK" "$STAGE_BIN/mount_root_rw" sbin/mount-root-rw
# Privileged helpers are small dedicated ELFs, never the multi-call BusyBox.
$INJECT "$DISK" --setuid "$STAGE_BIN/ir0_passwd" bin/passwd
$INJECT "$DISK" "$STAGE_BIN/ir0_status" bin/ir0-status
# OpenDoas: separate setuid-root elevador (never BusyBox).
if [ -f "$STAGE_BIN/doas" ]; then
	$INJECT "$DISK" --setuid "$STAGE_BIN/doas" usr/bin/doas
	$INJECT "$DISK" --mode 0440 \
		"${ROOT}/rootfs/etc/doas.conf" etc/doas.conf
fi
# Reduced BusyBox: only login/su. Never share the setuid bit with /bin/busybox.
$INJECT "$DISK" --setuid "$BUSYBOX_AUTH" usr/bin/busybox-auth
$INJECT --hardlink "$DISK" usr/bin/busybox-auth bin/login
$INJECT --hardlink "$DISK" usr/bin/busybox-auth bin/su

chmod +x "${ROOT}/scripts/busybox_inject_manifest.sh"
IR0_ROOT="$IR0_ROOT" FASE50_BUSYBOX_BIN="$BUSYBOX" "${ROOT}/scripts/busybox_inject_manifest.sh" "$DISK" "$BUSYBOX" "$MANIFEST"

echo "  RUNIT   Injecting stage ELF stubs (exec path has no shebang)..."
$INJECT "$DISK" "$STAGE_BIN/runit_stage1" etc/runit/1
$INJECT "$DISK" "$STAGE_BIN/runit_stage2" etc/runit/2
$INJECT "$DISK" "$STAGE_BIN/runit_stage3" etc/runit/3
$INJECT "$DISK" "$STAGE_BIN/runit_console_run" etc/runit/sv/console/run
$INJECT "$DISK" "$STAGE_BIN/runit_logger_run" etc/runit/sv/logger/run

echo "  RUNIT   Injecting account / issue files..."
$INJECT "$DISK" --mode 0644 "$ROOTFS_ETC/passwd" etc/passwd
# shadow(5) holds hashes: root-only, unlike passwd/group.
$INJECT "$DISK" --mode 0600 "$ROOTFS_ETC/shadow" etc/shadow
$INJECT "$DISK" --mode 0644 "$ROOTFS_ETC/group" etc/group
$INJECT "$DISK" "$ROOTFS_ETC/issue" etc/issue
$INJECT "$DISK" "$ROOTFS_ETC/hostname" etc/hostname
$INJECT "$DISK" "$ROOTFS_ETC/profile" etc/profile
# Product profile drives the console policy (see lib/ir0_profile.h): only the
# development image keeps the root autologin sentinel; desktop and appliance
# images always go through login (or no interactive login at all).
PROFILE_NAME="${IR0_PRODUCT_PROFILE:-development}"
case "$PROFILE_NAME" in
development|desktop|appliance) ;;
*)
	echo "✗ unknown IR0_PRODUCT_PROFILE=$PROFILE_NAME (development|desktop|appliance)" >&2
	exit 1
	;;
esac
if [ "$PROFILE_NAME" = development ]; then
	NO_AUTOLOGIN="${IR0_NO_AUTOLOGIN:-0}"
else
	NO_AUTOLOGIN=1
fi
printf '%s\n' "$PROFILE_NAME" > "/tmp/ir0-profile.$$"
$INJECT "$DISK" --mode 0644 "/tmp/ir0-profile.$$" etc/ir0-profile
rm -f "/tmp/ir0-profile.$$"
# Desktop interactive images refuse direct root login: the account is reached
# through doas from the wheel user created at first boot.
if [ "$PROFILE_NAME" = desktop ]; then
	printf '1\n' > "/tmp/ir0-root-deny.$$"
	$INJECT "$DISK" --mode 0644 "/tmp/ir0-root-deny.$$" etc/ir0-noroot
	rm -f "/tmp/ir0-root-deny.$$"
fi
if [ "$NO_AUTOLOGIN" != "1" ] && [ -f "$ROOTFS_ETC/ir0-autologin" ]; then
	$INJECT "$DISK" "$ROOTFS_ETC/ir0-autologin" etc/ir0-autologin
fi
# Optional matrix dump for `ir0-status busybox` (host-generated).
if [ -f "${ROOT}/packages/busybox/bb_status.tsv" ]; then
	$INJECT "$DISK" --mode 0644 \
		"${ROOT}/packages/busybox/bb_status.tsv" etc/busybox/bb_status.tsv
fi
$INJECT "$DISK" "${ROOT}/rootfs/root/.keep" root/.keep
$INJECT "$DISK" --mode 0644 --owner 1000:100 \
	"${ROOT}/rootfs/home/ivan/.keep" home/ivan/.keep
$INJECT --owner 1000:100 --mode 0700 --chown "$DISK" home/ivan

python3 "${IR0_ROOT}/scripts/verify_minix_rootfs.py" --gate "$DISK" \
	/sbin/init /sbin/runit /bin/runsvdir /bin/sh /bin/busybox \
	/sbin/fsck.ir0 /sbin/ir0-firstboot /sbin/ir0-recovery /sbin/mount-root-rw /bin/passwd \
	/usr/bin/busybox-auth /bin/login /bin/su \
	/etc/passwd /etc/shadow /etc/group \
	/etc/runit/1 /etc/runit/2 /etc/runit/3 \
	/etc/runit/sv/console/run /etc/runit/sv/logger/run

echo "✓ runit rootfs installed on $DISK"
