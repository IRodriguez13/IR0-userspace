# Release process

> **Last verified:** 2026-07-26

```bash
make fetch
make headers IR0_ROOT=../IR0   # or IR0_UAPI_TARBALL=...
make release-check PROFILE=minimal ARCH=x86_64
make rootfs-tar PROFILE=minimal ARCH=x86_64
```

`release-check` runs toolchain, elf, uapi, profiles, rootfs, host tests.
