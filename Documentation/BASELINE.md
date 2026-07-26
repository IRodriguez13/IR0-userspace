# Phase 0 baseline

> **Last verified:** 2026-07-26  
> **Source of truth:** `out/baseline/` logs, git pins below.

## Pins

| Tree | Commit | Subject |
|------|--------|---------|
| IR0-userspace | `6e311162c99273bcbb0ea4978b3257ba1c1231bd` | fix(console): exportar TERM=linux para nano/ncurses |
| IR0 (`origin/master`) | `15d6d3df831454c224f916fbba026203a65c2cd5` | fix: remaining Bugbot follow-ups for RTC, sysfs net, mandoc |

## Commands and results

| Check | Result |
|-------|--------|
| `make fetch` | OK (exit 0) |
| `make headers` (`IR0_ROOT=../IR0`) | OK |
| `make build` | OK |
| `make profiles-check` | OK |
| `make rootfs PROFILE=development` | OK |
| `make rootfs PROFILE=desktop` | OK |
| `make rootfs PROFILE=appliance` | OK |
| `make -C tests/host run` | OK |
| `make rootfs PROFILE=minimal` | N/A (pre-migration) |
| `make toolchain-check` | N/A (pre-migration) |

Logs: `out/baseline/*.log` (gitignored under `out/`).

## Post-migration gates (2026-07-26)

| Check | Result |
|-------|--------|
| `make build ARCH=x86_64` | OK |
| `make toolchain-check ARCH=x86_64` | OK (x86_64-linux-musl-gcc) |
| `make elf-audit` / `uapi-audit` / `profiles-check` | OK |
| `make personal-data-check` / `rootfs-check PROFILE=minimal` | OK |
| `make rootfs-tree` + `rootfs-tar` + `image-minix` minimal | OK |
| `image-minix PROFILE=development` | OK (labuser fixtures, doas+nano) |
| `make release-check PROFILE=minimal` | OK |
| `make -C tests/host run` | OK (1/1) |
| Dual `SOURCE_DATE_EPOCH` rootfs-tar | OK (identical SHA-256) |
| `ARCH=aarch64` package status | busybox/runit=buildable; opendoas/ncurses/nano=blocked-by-package |

## Honest residuals

| Item | Class |
|------|-------|
| aarch64 cross compiler not installed on this host | target partial |
| DHCP network mode | blocked-by-kernel-ABI |
| Guest `man` not in minimal by default | optional / Partial |
| BusyBox still sequential in-tree under package lock | improvement (no /tmp lock) |
