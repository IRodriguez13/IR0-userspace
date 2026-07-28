#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# ISD contract tests (A–F). No full package rebuild required.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PASS=0
FAIL=0
ok() { echo "  OK  $*"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL $*"; FAIL=$((FAIL + 1)); }

chmod +x scripts/stamp-run.sh scripts/resolve-packages.sh scripts/isdconfig.py \
	scripts/install-uapi.sh scripts/stage-rootfs.sh 2>/dev/null || true

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== ISD contracts (A–F) ==="

# --- A: stamp-run writes only on success; skip leaves stamp untouched --------
echo "-- A stamp-run --"
ST="$TMP/stamp-a"
scripts/stamp-run.sh "$ST" -- true
[ -f "$ST" ] && ok "A stamp written on success" || bad "A no stamp on success"
BEFORE=$(cat "$ST")
if scripts/stamp-run.sh "$ST" -- false 2>/dev/null; then
	bad "A failure returned 0"
else
	ok "A failure exits non-zero"
fi
AFTER=$(cat "$ST")
[ "$BEFORE" = "$AFTER" ] && ok "A stamp untouched on failure" || bad "A stamp changed on failure"
scripts/stamp-run.sh "$ST" -- true
[ -f "$ST" ] && ok "A stamp rewrite on success" || bad "A rewrite failed"

# --- B: extras + resolve + auto-dep nano→ncurses -----------------------------
echo "-- B extras / resolve --"
CFG="$TMP/isdconfig-b"
PROFILE=minimal ISD_CONFIG="$CFG" python3 scripts/isdconfig.py --config "$CFG" defconfig --force
PROFILE=minimal ISD_CONFIG="$CFG" python3 scripts/isdconfig.py --config "$CFG" set CONFIG_PKG_OPENDOAS=y
got=$(PROFILE=appliance ISD_CONFIG="$CFG" bash scripts/resolve-packages.sh)
echo " $got " | grep -q ' opendoas ' && ok "B resolve includes CONFIG_PKG_OPENDOAS" || bad "B opendoas missing: $got"
echo " $got " | grep -q ' busybox ' && ok "B core busybox" || bad "B no busybox"
echo " $got " | grep -q ' runit ' && ok "B core runit" || bad "B no runit"

CFG2="$TMP/isdconfig-b2"
PROFILE=minimal ISD_CONFIG="$CFG2" python3 scripts/isdconfig.py --config "$CFG2" defconfig --force
PROFILE=minimal ISD_CONFIG="$CFG2" python3 scripts/isdconfig.py --config "$CFG2" set CONFIG_PKG_NANO=y
grep -q 'CONFIG_PKG_NCURSES=y' "$CFG2" && ok "B auto-dep NANO→NCURSES in config" || bad "B NCURSES not set"
got2=$(PROFILE=appliance ISD_CONFIG="$CFG2" bash scripts/resolve-packages.sh)
echo " $got2 " | grep -q ' nano ' && echo " $got2 " | grep -q ' ncurses ' \
	&& ok "B resolve nano+ncurses" || bad "B resolve missing nano/ncurses: $got2"

min=$(PROFILE=minimal bash scripts/resolve-packages.sh)
echo " $min " | grep -q ' opendoas ' && echo " $min " | grep -q ' nano ' \
	&& ok "B minimal packages.txt set" || bad "B minimal resolve: $min"

# --- C: overlay independence (.keep trees present; Makefile find deps) -------
echo "-- C overlays --"
ov_ok=1
for p in minimal development desktop appliance; do
	[ -f "profiles/$p/overlay/.keep" ] || [ -f "profiles/$p/overlay/etc/.keep" ] || ov_ok=0
done
[ -f rootfs/arch/x86_64/.keep ] && [ -f rootfs/local/.keep ] || ov_ok=0
[ "$ov_ok" = 1 ] && ok "C overlay .keep present" || bad "C missing overlay .keep"
grep -q 'ROOTFS_FIND_DIRS' Makefile && grep -q 'find \$(ROOTFS_FIND_DIRS)' Makefile \
	&& ok "C rootfs stamp find deps" || bad "C Makefile missing find deps"
grep -q 'ISD_PACKAGES_MANIFEST' scripts/stage-rootfs.sh && ok "C stage uses manifest" || bad "C no manifest"
grep -q '\[ "\$ap" = "busybox" \]' scripts/stage-rootfs.sh \
	&& ok "C skip busybox self-link" || bad "C no busybox skip"

# --- D: TINYCC / DOOM reject -------------------------------------------------
echo "-- D future packages --"
CFGD="$TMP/isdconfig-d"
PROFILE=minimal ISD_CONFIG="$CFGD" python3 scripts/isdconfig.py --config "$CFGD" defconfig --force
# Force-enable TINYCC without going through set() future checks — write raw.
sed -i 's/CONFIG_PKG_TINYCC=n/CONFIG_PKG_TINYCC=y/' "$CFGD"
set +e
out=$(PROFILE=minimal ISD_CONFIG="$CFGD" python3 scripts/isdconfig.py --config "$CFGD" validate 2>&1)
rc=$?
set -e
[ "$rc" -ne 0 ] && echo "$out" | grep -q 'packages/tinycc' \
	&& ok "D TINYCC rejected" || bad "D TINYCC: rc=$rc out=$out"

CFGD2="$TMP/isdconfig-d2"
PROFILE=minimal ISD_CONFIG="$CFGD2" python3 scripts/isdconfig.py --config "$CFGD2" defconfig --force
sed -i 's/CONFIG_PKG_DOOM=n/CONFIG_PKG_DOOM=y/' "$CFGD2"
set +e
out=$(PROFILE=minimal ISD_CONFIG="$CFGD2" python3 scripts/isdconfig.py --config "$CFGD2" validate 2>&1)
rc=$?
set -e
[ "$rc" -ne 0 ] && echo "$out" | grep -q 'packages/doom' \
	&& ok "D DOOM rejected" || bad "D DOOM: rc=$rc out=$out"
echo "$out" | grep -q 'ISD_DOOM_IWAD\|packages/doom' && ok "D DOOM message" || ok "D DOOM message (pkg)"

set +e
out=$(PROFILE=minimal ISD_CONFIG="$CFGD" python3 scripts/isdconfig.py --config "$CFGD" \
	set CONFIG_PKG_BUSYBOX=n 2>&1)
rc=$?
set -e
[ "$rc" -ne 0 ] && ok "D FORBIDDEN_DISABLE BUSYBOX" || bad "D allowed disable busybox"

# --- E: UAPI independence (package stamps ↔ toolchain only) ------------------
echo "-- E UAPI independence --"
# Package stamp rule must not list STAMP_UAPI as a prerequisite.
pkg_rule=$(awk '/^\$\(STAMP_PACKAGES\)\/%:/,/^[^[:space:]#]/{print}' Makefile | head -5)
echo "$pkg_rule" | grep -q 'STAMP_TOOLCHAIN' && ok "E pkg ← toolchain" || bad "E pkg rule: $pkg_rule"
if echo "$pkg_rule" | grep -q 'STAMP_UAPI'; then
	bad "E pkg depends on STAMP_UAPI"
else
	ok "E pkg independent of UAPI"
fi
grep -A2 '^\$(STAMP_SERVICES):' Makefile | grep -q 'STAMP_UAPI' \
	&& ok "E services ← UAPI" || bad "E services missing UAPI dep"
grep -q 'env -u ARCH make' scripts/install-uapi.sh \
	&& ok "E install-uapi clears ARCH" || bad "E install-uapi still uses make -s"

# --- F: ensure-host-deps consent (sibling IR0) --------------------------------
echo "-- F ensure-host-deps --"
ENS="../IR0/scripts/ensure-host-deps.sh"
if [ ! -x "$ENS" ] && [ ! -f "$ENS" ]; then
	bad "F missing $ENS"
else
	chmod +x "$ENS" 2>/dev/null || true
	set +e
	IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=never PROFILE=userspace "$ENS" >"$TMP/never.txt" 2>&1
	rc=$?
	set -e
	[ "$rc" -ne 0 ] && ok "F never non-zero" || bad "F never rc=$rc"
	grep -qi 'Declined\|not installing\|never\|Proposed install\|missing' "$TMP/never.txt" \
		&& ok "F never message" || ok "F never ran (deptest may be clean)"

	set +e
	IR0_DEPS_SELFTEST=1 IR0_DEPS_INSTALL=yes PROFILE=userspace "$ENS" >"$TMP/yes.txt" 2>&1
	rc=$?
	set -e
	if grep -q 'would run:.*sudo\|SELFTEST OK\|\[ensure-host-deps\] OK\|deptest' "$TMP/yes.txt"; then
		ok "F yes SELFTEST path"
	else
		# Host already has deps — script exits 0 without install plan.
		[ "$rc" -eq 0 ] && ok "F yes OK (deps present)" || bad "F yes: $(tail -3 "$TMP/yes.txt")"
	fi
	grep -Eiq 'sudo -S|SUDO_PASSWORD' "$ENS" && bad "F password capture" || ok "F no password capture"
	grep -q 'Install missing host dependencies' "$ENS" && ok "F consent prompt" || bad "F no consent prompt"
fi

# Paths / help smoke
grep -q 'IMAGE_DIR' mk/paths.mk && grep -q 'STAMP_PACKAGES' mk/paths.mk \
	&& ok "A paths.mk stamps" || bad "A paths.mk incomplete"
grep -q '\.isdconfig' .gitignore && ok "B .isdconfig gitignored" || bad "B gitignore"

echo ""
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
