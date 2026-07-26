# Firstboot

> **Last verified:** 2026-07-26  
> **Source of truth:** `services/ir0_firstboot.c`

| Mode | Behavior |
|------|----------|
| Interactive | Prompt username/hostname/password; no default username |
| `FIRSTBOOT_SEED=/path` | Non-interactive; requires `username` + `password_hash` |
| Development | Lab seed `labuser` + warning (fixtures only) |
| No TTY, no seed | Pending (`FIRSTBOOT_PENDING`); no implicit account |

Done markers: `/var/lib/ir0/firstboot.done` and `/etc/ir0-firstboot-done`.
