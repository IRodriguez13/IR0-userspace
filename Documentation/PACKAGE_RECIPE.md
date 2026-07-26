# Package recipes

> **Last verified:** 2026-07-26

Each `packages/<pkg>/` provides `version`, `url`, `sha256`, `build.sh`.
Build tools come only from `scripts/toolchain.sh`.

Setuid allowlist: `packages/setuid.allowlist`.
Install path: `scripts/stage-rootfs.sh` → DESTDIR tree (not MINIX-aware).
