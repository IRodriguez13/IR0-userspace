# Product profiles

> **Last verified:** 2026-07-26  
> **Source of truth:** `profiles/<name>/`

Each profile directory contains:

| File | Role |
|------|------|
| `profile.conf` | Product policy (login, root, packages flags, network) |
| `packages.txt` | Packages to build/install |
| `applets.txt` | BusyBox applets linked into the rootfs |
| `services.txt` | Enabled runit services |
| `overlay/` | Optional file overlay |

| Profile | Login | Root | Packages (extra) | Purpose |
|---------|-------|------|------------------|---------|
| minimal | firstboot | locked | — | Canonical distro |
| development | root autologin | lab empty pw | doas nano ncurses | Lab / smokes |
| desktop | firstboot | noroot | doas nano ncurses | IR0-desktop base |
| appliance | none | locked | — | Headless services |

Default: `PROFILE=minimal`.
