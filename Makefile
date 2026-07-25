# SPDX-License-Identifier: GPL-3.0-only
#
# IR0-userspace — the Unix userland that runs on top of the IR0 kernel.
#
#   make fetch     download + checksum upstream sources (network)
#   make headers   import kernel UAPI into sysroot/ (needs IR0_ROOT)
#   make build     build packages and services (offline after fetch)
#   make rootfs    install the rootfs into a MINIX disk image
#   make image     rootfs + bootable kernel ISO from the kernel tree
#
# The kernel tree is only used for image tooling (MINIX injection, ISO) and
# UAPI headers; no kernel source is compiled here.

# profiles-check uses process substitution.
SHELL := /bin/bash

IR0_ROOT ?= $(abspath $(CURDIR)/../IR0)
DISK     ?= $(CURDIR)/out/disk.img
DISK_MB  ?= 200
PROFILE  ?= development

PACKAGES  = busybox runit opendoas
OUT       = $(CURDIR)/out
SYSROOT   = $(CURDIR)/sysroot

MUSL_CC ?= $(shell command -v x86_64-linux-musl-gcc 2>/dev/null || command -v musl-gcc 2>/dev/null)
export MUSL_CC

.PHONY: all fetch headers build build-packages build-services disk rootfs image \
	profiles-check clean distclean help check-kernel \
	$(addprefix build-,$(PACKAGES))

all: build

help:
	@echo "IR0-userspace targets: fetch headers build rootfs image profiles-check clean"
	@echo "  IR0_ROOT=$(IR0_ROOT)"
	@echo "  PROFILE=$(PROFILE) (development | desktop | appliance)"

check-kernel:
	@if [ ! -f "$(IR0_ROOT)/scripts/inject_init_minix.py" ]; then \
		echo "✗ kernel tree not found at IR0_ROOT=$(IR0_ROOT)"; \
		echo "  export IR0_ROOT=/path/to/IR0"; \
		exit 1; \
	fi

fetch:
	@chmod +x scripts/fetch-package.sh
	@for p in $(PACKAGES); do scripts/fetch-package.sh $$p; done

headers: check-kernel
	@$(MAKE) -s -C $(IR0_ROOT) headers_install DESTDIR=$(SYSROOT)

build: build-packages build-services

build-packages: $(addprefix build-,$(PACKAGES))

# Per-package entry points so the kernel tree can request just what a gate
# needs without two make jobs racing on the same source tree.
$(addprefix build-,$(PACKAGES)): build-%:
	@chmod +x packages/$*/build.sh
	@packages/$*/build.sh

build-services:
	@chmod +x scripts/build-services.sh
	@scripts/build-services.sh

$(DISK): | check-kernel
	@mkdir -p $(dir $(DISK))
	@echo "  DISK    $(DISK) ($(DISK_MB)M MINIX)"
	@dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_MB) status=none
	@python3 $(IR0_ROOT)/scripts/inject_init_minix.py --format-large $(DISK)

disk: $(DISK)

rootfs: check-kernel $(DISK)
	@chmod +x scripts/install-to-disk.sh
	@IR0_ROOT=$(IR0_ROOT) IR0_PRODUCT_PROFILE=$(PROFILE) \
		scripts/install-to-disk.sh $(DISK)

image: rootfs
	@$(MAKE) -s -C $(IR0_ROOT) kernel-x64-userspace.iso
	@echo "✓ image ready: kernel $(IR0_ROOT)/kernel-x64-userspace.iso + rootfs $(DISK)"

# Applet profiles must stay a subset of what the full BusyBox binary provides.
profiles-check:
	@set -e; \
	full=$(OUT)/busybox-full; \
	if [ ! -x "$$full" ]; then echo "✗ missing $$full (make build)"; exit 1; fi; \
	$$full --list | sort > /tmp/ir0-us-applets.txt; \
	for prof in profiles/*.txt; do \
		[ -f "$$prof" ] || continue; \
		missing=$$(comm -23 <(grep -vE '^\s*(#|$$)' $$prof | sort) /tmp/ir0-us-applets.txt); \
		if [ -n "$$missing" ]; then \
			echo "✗ $$prof requires applets absent from busybox-full:"; \
			echo "$$missing"; exit 1; \
		fi; \
		echo "  $$(basename $$prof .txt): $$(grep -cvE '^\s*(#|$$)' $$prof) applets"; \
	done; \
	echo "✓ profiles-check OK"

clean:
	@rm -rf $(OUT)
	@echo "✓ clean (out/ removed; sources kept)"

distclean: clean
	@rm -rf $(SYSROOT) packages/*/src
	@echo "✓ distclean (sources and sysroot removed; dist/ tarballs kept)"
