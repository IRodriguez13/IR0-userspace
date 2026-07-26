# Toolchain and targets

> **Last verified:** 2026-07-26  
> **Source of truth:** `scripts/toolchain.sh`, `mk/toolchain.mk`

```bash
make toolchain-check ARCH=x86_64
make build ARCH=x86_64
make build ARCH=aarch64   # honest skip/block per package
make elf-audit ARCH=x86_64
```

Variables: `ARCH`, `TARGET_TRIPLE`, `CROSS_COMPILE`, `CC`, `AR`, `RANLIB`,
`STRIP`, `READELF`, `OBJCOPY`, `SYSROOT`, `IR0_UAPI_SYSROOT`.

Outputs: `out/<arch>/{product,tests,smoke,rootfs/<profile>/}`.

| ARCH | busybox/runit | opendoas/ncurses/nano |
|------|---------------|------------------------|
| x86_64 | supported | supported |
| aarch64 | buildable | blocked-by-package |
