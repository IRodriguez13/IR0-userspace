# Rootfs layout

> **Last verified:** 2026-07-26

Primary artifact: `out/<arch>/rootfs/<profile>/`.

Required directories: `/bin` `/sbin` `/usr/bin` `/usr/sbin` `/etc` `/dev`
`/proc` `/sys` `/heart` `/run` `/tmp` `/var` `/home` `/root` `/mnt`.

Modes: `/tmp` 01777, `/run` 0755, `/root` 0700, `/etc/shadow` 0600.

`/etc/os-release` is generated from `VERSION`.
