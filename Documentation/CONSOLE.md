# Console / getty session model

> **Last verified:** 2026-07-30  
> **Source of truth:** `services/runit_console_run.c`, IR0 smoke
> `../IR0/scripts/smoke_console_session_fork.py`

## Problem

BusyBox ash under the console service used to **`exec`** the login shell. A
`SIGSEGV` (or any fatal signal) then killed the runit `console` service itself,
triggering a full respawn (`RUNSV_CONSOLE_START`) and another login cycle.

## Current model

`services/runit_console_run.c`:

1. Parent keeps the runit service alive.
2. **`fork`** a child for each interactive session.
3. Child: `setsid`, `TIOCSCTTY`, `TIOCSPGRP`, drop privileges, `exec` `-sh -i`.
4. Parent: `waitpid`; then:
   - **Clean exit** (`exit` / Ctrl-D → status 0) → login prompt (`REPROMPT`)
   - **SEGV / signal / non-zero exit** → **same user**, new shell (`RESUME`) — no
     password again

Serial tags: `CONSOLE_SESSION_START` / `END` / `SEGV` / `SIGNAL` / `RESUME` /
`REPROMPT`.

## Keyboard layout

On start (and again after firstboot), the console applies `/etc/keymap` via IR0
syscalls `keymap_set` / `keymap_get` (`us` or `latam`). Users change it with
`/usr/bin/keymap`. Firstboot wizard prompts for the layout on minimal/desktop.

## Pack / applets notes

- `scripts/pack-minix.sh` always `format-large` so a stale `firstboot.done` cannot
  brick login against a reset passwd.
- Development/desktop `applets.txt` track BusyBox-full applet lists (includes
  `top`). Kernel must expose `/proc/stat` + digit `/proc` dirents (IR0).

## Validation

- IR0: `python3 scripts/smoke_console_session_fork.py` (injects rebuilt console
  `run` into a copy of the development disk).
- Manual: `make run PROFILE=development` → login → `exit` → login prompt again
  without a second `RUNSV_CONSOLE_START`. Shell SEGV → new shell, same user
  (`CONSOLE_SESSION_RESUME`), no password.
