# IR0-userspace

The Unix userland of IR0/Unix: PID 1 (runit), console getty and login, BusyBox,
OpenDoas, firstboot, recovery and the `/etc` overlays that turn the kernel's
mechanisms into a usable system.

The kernel lives in the sibling repository [`IR0`](../IR0). Nothing here is
compiled into the kernel: if PID 1 can replace it without recompiling the
kernel, it belongs in this repository.

## Layout

```text
packages/       upstream recipes: version, url, sha256, patches/, build.sh
  busybox/      applet configs (full 0755 + auth 4755) and manifests
  runit/        PID 1 and supervision
  opendoas/     setuid-root elevation (permit persist :wheel as root)
lib/            ir0_auth (shadow/crypt), profile and smoke-tag helpers
services/       runit stages, console getty/login, firstboot, passwd, recovery
rootfs/         /etc overlays shipped in the image
profiles/       BusyBox applet sets per product profile
scripts/        rootfs installer and BusyBox manifest tooling
smoke/          product test drivers (doas, passwd, applet matrix)
sysroot/        kernel UAPI imported by `make headers` (not versioned)
out/            built binaries and disk images (not versioned)
```

## Build

```bash
export IR0_ROOT=../IR0            # kernel tree (image tooling + UAPI)
make fetch                        # download + verify upstream sources
make headers                      # import kernel UAPI into sysroot/
make build                        # busybox, runit, opendoas, services
make rootfs DISK=out/disk.img     # install the rootfs into a MINIX image
make image                        # rootfs + bootable kernel ISO
```

`make build` works offline once `make fetch` has verified the checksums in
`packages/<pkg>/sha256`.

## Product profiles

| Profile | Console |
|---------|---------|
| `development` | root autologin with a visible warning; wide applet set; smoke markers on serial |
| `desktop` | firstboot once, then `hostname login:` + password, drop to the unprivileged user; direct root login refused; elevation via `doas` |
| `appliance` | no interactive login; services only; recovery through the kernel command line |

Select one when installing the rootfs:

```bash
make rootfs PROFILE=desktop
```

## Relationship with the kernel repository

The kernel exposes its stable userspace ABI through
`make -C $IR0_ROOT headers_install DESTDIR=<sysroot>`. A package that needs a
private kernel header is a broken boundary, not a missing include path.

Kernel-side smokes call back into this repository through
`IR0_USERSPACE_ROOT`, so the kernel tree keeps its gates without carrying
userspace sources.
