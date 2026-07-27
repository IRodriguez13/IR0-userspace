# SPDX-License-Identifier: GPL-3.0-only
#
# IR0-userspace — declarative builder for the canonical IR0/Unix distro.
#
#   make fetch
#   make headers                 # IR0_ROOT=../IR0  or IR0_UAPI_TARBALL=...
#   make build ARCH=x86_64
#   make rootfs-tree PROFILE=minimal ARCH=x86_64
#   make image-minix PROFILE=minimal ARCH=x86_64
#
# Kernel tree is only used for UAPI export and optional image adapters.

SHELL := /bin/bash

IR0_ROOT ?= $(abspath $(CURDIR)/../IR0)
DISK     ?= $(CURDIR)/out/disk.img
DISK_MB  ?= 200
PROFILE  ?= minimal
ARCH     ?= x86_64

# Ensure out/ exists before toolchain stamp generation.
$(shell mkdir -p $(CURDIR)/out)
# Make's default CC=cc must not shadow the musl toolchain facade.
ifeq ($(origin CC),default)
  CC :=
endif
include mk/toolchain.mk

ALL_PACKAGES = busybox runit opendoas ncurses nano
PACKAGES ?= $(ALL_PACKAGES)

.PHONY: all fetch headers build build-packages build-services build-tests \
	disk rootfs rootfs-tree rootfs-manifest rootfs-tar image-minix image \
	profiles-check toolchain-check elf-audit uapi-audit personal-data-check \
	rootfs-check release-check clean distclean help check-kernel \
	compat-links $(addprefix build-,$(ALL_PACKAGES))

all: build

help:
	@echo "IR0-userspace — canonical distro builder"
	@echo "  ARCH=$(ARCH)  PROFILE=$(PROFILE)  IR0_ROOT=$(IR0_ROOT)"
	@echo "  Targets: fetch headers build toolchain-check elf-audit"
	@echo "           rootfs-tree rootfs-tar image-minix image rootfs"
	@echo "           profiles-check personal-data-check rootfs-check release-check"

check-kernel:
	@if [ ! -f "$(IR0_ROOT)/scripts/inject_init_minix.py" ]; then \
		echo "✗ kernel tree not found at IR0_ROOT=$(IR0_ROOT)"; \
		echo "  export IR0_ROOT=/path/to/IR0  (only needed for MINIX/ISO adapters)"; \
		exit 1; \
	fi

fetch:
	@chmod +x scripts/fetch-package.sh
	@for p in $(ALL_PACKAGES); do scripts/fetch-package.sh $$p; done

# UAPI: sibling tree, tarball, or pre-populated IR0_UAPI_SYSROOT.
headers:
	@chmod +x scripts/install-uapi.sh
	@IR0_ROOT="$(IR0_ROOT)" IR0_UAPI_TARBALL="$(IR0_UAPI_TARBALL)" \
		IR0_UAPI_SYSROOT="$(IR0_UAPI_SYSROOT)" \
		scripts/install-uapi.sh

build: build-packages build-services compat-links

build-packages: $(addprefix build-,$(PACKAGES))

$(addprefix build-,$(ALL_PACKAGES)): build-%:
	@chmod +x packages/$*/build.sh scripts/toolchain.sh
	@status=$$(ARCH=$(ARCH) bash -c 'source scripts/toolchain.sh && toolchain_pkg_status $*'); \
	echo "  PKG     $* [$$status] ARCH=$(ARCH)"; \
	case "$$status" in \
		unsupported) echo "✗ $* unsupported on ARCH=$(ARCH)"; exit 1 ;; \
		blocked-by-package|blocked-by-kernel-ABI) \
			echo "  SKIP    $* ($$status)"; exit 0 ;; \
	esac; \
	ARCH=$(ARCH) CC="$(CC)" MUSL_CC="$(CC)" PRODUCT_OUT="$(PRODUCT_OUT)" \
		OUT="$(PRODUCT_OUT)" SYSROOT="$(SYSROOT)" \
		packages/$*/build.sh

build-services:
	@chmod +x scripts/build-services.sh scripts/toolchain.sh
	@ARCH=$(ARCH) CC="$(CC)" MUSL_CC="$(CC)" PRODUCT_OUT="$(PRODUCT_OUT)" \
		SMOKE_OUT="$(SMOKE_OUT)" SYSROOT="$(SYSROOT)" \
		scripts/build-services.sh product

build-tests:
	@chmod +x scripts/build-services.sh
	@ARCH=$(ARCH) CC="$(CC)" MUSL_CC="$(CC)" PRODUCT_OUT="$(PRODUCT_OUT)" \
		SMOKE_OUT="$(SMOKE_OUT)" TESTS_OUT="$(TESTS_OUT)" SYSROOT="$(SYSROOT)" \
		scripts/build-services.sh smoke
	@$(MAKE) -s -C tests/host run

# Legacy paths expected by the kernel tree (symlinks into out/<arch>/product).
compat-links:
	@mkdir -p out "$(SMOKE_OUT)" "$(TESTS_OUT)"
	@ln -sfn "$(PRODUCT_OUT)/busybox-full" out/busybox-full
	@ln -sfn "$(PRODUCT_OUT)/busybox-auth" out/busybox-auth
	@ln -sfn "$(PRODUCT_OUT)/bin" out/bin
	@ln -sfn "$(PRODUCT_OUT)/stage-bin" out/stage-bin
	@ln -sfn "$(SMOKE_OUT)" out/smoke
	@ln -sfn "$(PRODUCT_OUT)" out/product

toolchain-check:
	@chmod +x scripts/toolchain.sh
	@ARCH=$(ARCH) bash -c 'source scripts/toolchain.sh && \
		echo "ARCH=$$ARCH TARGET_TRIPLE=$$TARGET_TRIPLE"; \
		echo "CC=$$CC"; $$CC --version | head -1; \
		echo "READELF=$$READELF"; \
		echo "✓ toolchain-check OK"'

elf-audit: compat-links
	@chmod +x scripts/elf-audit.sh
	@ARCH=$(ARCH) READELF="$(READELF)" PRODUCT_OUT="$(PRODUCT_OUT)" \
		scripts/elf-audit.sh

uapi-audit:
	@chmod +x scripts/uapi-audit.sh
	@IR0_UAPI_SYSROOT="$(IR0_UAPI_SYSROOT)" scripts/uapi-audit.sh

personal-data-check:
	@chmod +x scripts/personal-data-check.sh
	@PROFILE=$(PROFILE) ARCH=$(ARCH) ROOTFS_OUT="$(ROOTFS_OUT)" \
		scripts/personal-data-check.sh

profiles-check: compat-links
	@chmod +x scripts/profiles-check.sh
	@PRODUCT_OUT="$(PRODUCT_OUT)" scripts/profiles-check.sh

rootfs-tree: build
	@chmod +x scripts/stage-rootfs.sh
	@IR0_ROOT="$(IR0_ROOT)" IR0_PRODUCT_PROFILE="$(PROFILE)" ARCH="$(ARCH)" \
		PRODUCT_OUT="$(PRODUCT_OUT)" ROOTFS_OUT="$(ROOTFS_OUT)" \
		IR0_GUEST_MANDOC_DIR="$(IR0_GUEST_MANDOC_DIR)" \
		scripts/stage-rootfs.sh "$(ROOTFS_OUT)/$(PROFILE)"

rootfs-manifest: rootfs-tree
	@chmod +x scripts/rootfs-manifest.sh
	@scripts/rootfs-manifest.sh "$(ROOTFS_OUT)/$(PROFILE)" \
		"$(ROOTFS_OUT)/$(PROFILE).manifest"

rootfs-tar: rootfs-manifest
	@tar --sort=name --owner=0 --group=0 --numeric-owner \
		--mtime="@$${SOURCE_DATE_EPOCH:-0}" \
		-C "$(ROOTFS_OUT)/$(PROFILE)" -cf "$(ROOTFS_OUT)/$(PROFILE).tar" .
	@echo "✓ rootfs-tar $(ROOTFS_OUT)/$(PROFILE).tar"

$(DISK): | check-kernel
	@mkdir -p $(dir $(DISK))
	@echo "  DISK    $(DISK) ($(DISK_MB)M MINIX)"
	@dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_MB) status=none
	@python3 $(IR0_ROOT)/scripts/inject_init_minix.py --format-large $(DISK)

disk: $(DISK)

# Primary image path: finished tree → MINIX adapter.
image-minix: check-kernel rootfs-tree $(DISK)
	@chmod +x scripts/pack-minix.sh
	@IR0_ROOT="$(IR0_ROOT)" ARCH="$(ARCH)" PROFILE="$(PROFILE)" \
		scripts/pack-minix.sh "$(ROOTFS_OUT)/$(PROFILE)" "$(DISK)"

# Backward-compatible alias used by the kernel tree.
rootfs: image-minix

image: image-minix
	@$(MAKE) -s -C $(IR0_ROOT) kernel-x64-userspace.iso
	@echo "✓ image ready: $(IR0_ROOT)/kernel-x64-userspace.iso + $(DISK)"

rootfs-check: rootfs-tree personal-data-check
	@chmod +x scripts/rootfs-check.sh
	@PROFILE=$(PROFILE) ARCH=$(ARCH) READELF="$(READELF)" \
		scripts/rootfs-check.sh "$(ROOTFS_OUT)/$(PROFILE)"

release-check: toolchain-check elf-audit uapi-audit profiles-check \
	rootfs-check rootfs-manifest
	@$(MAKE) -s -C tests/host run
	@echo "✓ release-check OK PROFILE=$(PROFILE) ARCH=$(ARCH)"

clean:
	@rm -rf out
	@echo "✓ clean (out/ removed; sources kept)"

distclean: clean
	@rm -rf $(SYSROOT) packages/*/src
	@echo "✓ distclean (sources and sysroot removed; dist/ tarballs kept)"
