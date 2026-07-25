#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build the IR0/Unix service and helper ELFs (runit stages, console getty,
# firstboot, passwd, recovery, power helpers) as static musl binaries.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SVC="${ROOT}/services"
LIB="${ROOT}/lib"
OUT="${ROOT}/out/stage-bin"
SYSROOT="${ROOT}/sysroot"
CC="${MUSL_CC:-$(command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null || true)}"

if [ -z "$CC" ]; then
	echo "✗ musl cross compiler not found (set MUSL_CC=...)" >&2
	exit 1
fi

mkdir -p "$OUT"
AUTH_LIB="${LIB}/ir0_auth.c"
CFLAGS="-static -Os -I${LIB}"
# Kernel UAPI comes from `make headers_install` in the kernel tree; it is only
# added when present so a plain userspace build does not require the sibling.
[ -d "${SYSROOT}/usr/include" ] && CFLAGS="$CFLAGS -isystem ${SYSROOT}/usr/include"

cc_simple()
{
	# shellcheck disable=SC2086
	"$CC" $CFLAGS -o "${OUT}/$1" "${SVC}/$2" "${@:3}"
}

cc_simple runit_stage1 runit_stage1.c
cc_simple runit_stage2 runit_stage2.c
cc_simple runit_stage3 runit_stage3.c
cc_simple runit_console_run runit_console_run.c "$AUTH_LIB"
cc_simple runit_logger_run runit_logger_run.c
cc_simple fsck.ir0 fsck.ir0.c
cc_simple ir0_firstboot ir0_firstboot.c "$AUTH_LIB"
cc_simple ir0_passwd ir0_passwd.c "$AUTH_LIB"
cc_simple ir0_status ir0_status.c
cc_simple ir0_recovery ir0_recovery.c
cc_simple mount_root_rw mount_root_rw.c
cc_simple runit_fase55d_init runit_fase55d_init.c
cc_simple runit_power_smoke runit_power_smoke.c
cc_simple runit_power_run runit_power_run.c
cc_simple runit_busybox_halt_smoke runit_busybox_halt_smoke.c
cc_simple runit_busybox_poweroff_smoke runit_busybox_poweroff_smoke.c
cc_simple runit_busybox_reboot_smoke runit_busybox_reboot_smoke.c
cc_simple ir0_force_power ir0_force_power.c
cc_simple runit_hostshare_payload_run runit_hostshare_payload_run.c
cc_simple runit_pause_run runit_pause_run.c

# runit_exec_run.c is one source parameterised per supervised service.
exec_run()
{
	# shellcheck disable=SC2086
	"$CC" $CFLAGS -DRUNIT_EXEC_PATH="\"$2\"" -DRUNIT_START_TAG="\"$3\\n\"" \
		-o "${OUT}/$1" "${SVC}/runit_exec_run.c"
}

exec_run runit_fase52_run /bin/f52-harness RUNSV_FASE52_START
exec_run runit_tcc_power_run /bin/tccph RUNSV_TCC_POWER_START
exec_run runit_fase55d_run /bin/doom-smoke RUNSV_FASE55D_START
exec_run runit_busybox_halt_run /bin/bb-halt RUNSV_BUSYBOX_HALT_START
exec_run runit_busybox_poweroff_run /bin/bb-pwroff RUNSV_BUSYBOX_POWEROFF_START
exec_run runit_busybox_reboot_run /bin/bb-reboot RUNSV_BUSYBOX_REBOOT_START

for bin in "$OUT"/*; do
	file "$bin" | grep -q ELF
done
echo "✓ build services OK ($(ls -1 "$OUT" | wc -l) binaries)"
