# UAPI boundary

> **Last verified:** 2026-07-26

```bash
make headers IR0_ROOT=../IR0
make headers IR0_UAPI_TARBALL=/path/ir0-uapi.tar
make build IR0_UAPI_SYSROOT=/path/sysroot
make uapi-audit
```

Packages must not include private kernel headers (`kernel/`, `mm/`, `arch/`, …).
Release metadata: `sysroot/usr/share/ir0/uapi-release.txt`.
