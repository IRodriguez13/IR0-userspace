# ISD packages, stamps, and truth model

> **Last verified:** 2026-07-28  
> **Source of truth:** `Makefile`, `mk/paths.mk`, `scripts/resolve-packages.sh`,
> `scripts/isdconfig.py`, `scripts/stage-rootfs.sh`.

## Truth model

| Layer | File | Role |
|-------|------|------|
| Profile mandatory set | `profiles/<profile>/packages.txt` | Packages always built/installed for that profile |
| Extras | `.isdconfig` (`CONFIG_PKG_*`, `CONFIG_APPLET_*`) | Optional packages + BusyBox applet links; `make isdconfig` (interactive) / `isd-defconfig` |
| Core | busybox + runit | Always on; cannot disable |
| Policy only | `profiles/<profile>/profile.conf` | Login/root/fsck/network — **not** package truth |

Resolver (`scripts/resolve-packages.sh`):

```text
core (busybox runit)
  ∪ profiles/$PROFILE/packages.txt
  ∪ .isdconfig CONFIG_PKG_*=y
  + auto-dep nano → ncurses
```

Applets: profile `applets.txt` plus `.isdconfig` `CONFIG_APPLET_*=y` (e.g. `CONFIG_APPLET_TOP=y` → `/bin/top`). BusyBox must include the applet (`CONFIG_TOP=y` in `packages/busybox/ir0_full.config`).

`profiles/*/packages.txt` is lean (busybox+runit). Common extras default to **y** in `isd-defconfig` (nano, ncurses, opendoas, top).

## Stamp layout

```text
out/<arch>/stamps/
  toolchain/ok
  uapi/headers
  packages/<pkg>
  services/product
  rootfs/<profile>
  images/<profile>
```

| Stamp | Depends on | Notes |
|-------|------------|-------|
| **packages/**\* | toolchain **only** | Independent of UAPI — recipes use musl (+ host Linux headers where needed) |
| **services/product** | toolchain + UAPI | `build-services.sh` needs `-isystem sysroot/usr/include` |
| **rootfs/\<profile\>** | package stamps + services + UAPI + stage inputs / overlays | Overlay files feed the Make graph via `find` |
| **images/\<profile\>** | rootfs stamp | `disk.img` under `out/<arch>/images/<profile>/` |

`scripts/stamp-run.sh` writes a stamp **only on success**. A failed recipe leaves the previous stamp (if any) untouched so Make retries.

## Overlays → rootfs

`stage-rootfs.sh` layers (missing dirs are no-ops):

1. `rootfs/base`
2. legacy `rootfs/` (`etc`, `root`, …)
3. `profiles/<profile>/overlay`
4. `rootfs/arch/<arch>`
5. `rootfs/local` (gitignored personal overrides)

Keep empty overlay dirs with `.keep` so the tree exists in git.

## Packaged extras

| CONFIG | Recipe | Guest install |
|--------|--------|---------------|
| `CONFIG_PKG_NANO` | `packages/nano/` | `/usr/bin/nano` (+ auto `ncurses`) |
| `CONFIG_PKG_NCURSES` | `packages/ncurses/` | (library for nano) |
| `CONFIG_PKG_OPENDOAS` | `packages/opendoas/` | `/usr/bin/doas` |
| `CONFIG_PKG_GNUMAKE` | `packages/gnumake/` | `/usr/bin/make`, `/bin/make` |
| `CONFIG_PKG_TINYCC` | `packages/tinycc/` | `/usr/bin/tcc`, `/lib/tcc/`, musl CRT/headers |

`make fetch` downloads into `packages/<name>/dist` and unpacks to
`packages/<name>/src` only when missing (safe to re-run offline).

`make clean` removes **`out/` only** (stamps + staged binaries). It never
deletes `packages/*/src` or `packages/*/dist`. `make distclean` also drops
unpacked `src/` but keeps downloaded tarballs in `dist/`.

## Future extras

`CONFIG_PKG_DOOM` stays off until `packages/doom/` exists. Enabling it in
`.isdconfig` is scrubbed to `n` on validate (with a warning). Doom will also
require `ISD_DOOM_IWAD` pointing at a real IWAD file once packaged.
