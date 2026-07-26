# IR0-userspace

Declarative builder for the **canonical minimal IR0/Unix** distribution:
runit PID 1, BusyBox, login/auth, firstboot, recovery, and `/etc` overlays.

Sibling kernel: [`IR0`](https://github.com/IRodriguez13/IR0) — used only for
public UAPI (`headers_install`), optional MINIX/ISO adapters, and integration
smokes. See [`Documentation/DISTRO_CONTRACT.md`](Documentation/DISTRO_CONTRACT.md).

```bash
git clone https://github.com/IRodriguez13/IR0.git
git clone https://github.com/IRodriguez13/IR0-userspace.git
export IR0_ROOT=$PWD/IR0
cd IR0-userspace
make fetch
make headers
make build ARCH=x86_64
make rootfs-tree PROFILE=minimal ARCH=x86_64
make image-minix PROFILE=minimal ARCH=x86_64
```

Without a sibling checkout:

```bash
make headers IR0_UAPI_TARBALL=/path/ir0-uapi.tar
make build ARCH=x86_64
make rootfs-tar PROFILE=minimal ARCH=x86_64
```

## Profiles

| Profile | Role |
|---------|------|
| `minimal` | **Default** — canonical interactive distro |
| `development` | Lab (root autologin warning, fixtures) |
| `desktop` | minimal + doas/nano for IR0-desktop |
| `appliance` | Services only |

## Layout

```text
packages/        upstream recipes + setuid.allowlist
profiles/        profile.conf, packages, applets, services, overlay
rootfs/base/     canonical /etc (no personal accounts)
scripts/         toolchain, stage-rootfs, pack-minix, audits
services/        runit stages, console, firstboot, …
out/<arch>/      product/ tests/ smoke/ rootfs/<profile>/
Documentation/   distro contract and guides
```

## Gates

```bash
make toolchain-check ARCH=x86_64
make profiles-check
make personal-data-check
make rootfs-check PROFILE=minimal
make release-check PROFILE=minimal ARCH=x86_64
```
