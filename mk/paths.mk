# SPDX-License-Identifier: GPL-3.0-only
# ISD output and stamp layout (included by the top-level Makefile).
#
#   out/<arch>/
#     product/          package + service binaries
#     rootfs/<profile>/ finished tree
#     images/<profile>/ disk.img
#     stamps/{toolchain,uapi,packages,services,rootfs,images}/

ARCH     ?= x86_64
PROFILE  ?= minimal

OUT_ARCH    := $(CURDIR)/out/$(ARCH)
PRODUCT_OUT := $(OUT_ARCH)/product
ROOTFS_OUT  := $(OUT_ARCH)/rootfs
ROOTFS_DIR  := $(ROOTFS_OUT)/$(PROFILE)
IMAGE_DIR   := $(OUT_ARCH)/images/$(PROFILE)
DISK        := $(IMAGE_DIR)/disk.img

TESTS_OUT ?= $(OUT_ARCH)/tests
SMOKE_OUT ?= $(OUT_ARCH)/smoke

STAMP_DIR       := $(OUT_ARCH)/stamps
STAMP_TOOLCHAIN := $(STAMP_DIR)/toolchain/ok
STAMP_UAPI      := $(STAMP_DIR)/uapi/headers
STAMP_PACKAGES  := $(STAMP_DIR)/packages
STAMP_SERVICES  := $(STAMP_DIR)/services/product
STAMP_ROOTFS    := $(STAMP_DIR)/rootfs/$(PROFILE)
STAMP_IMAGE     := $(STAMP_DIR)/images/$(PROFILE)

export OUT_ARCH PRODUCT_OUT ROOTFS_OUT ROOTFS_DIR IMAGE_DIR DISK
export TESTS_OUT SMOKE_OUT
export STAMP_DIR STAMP_TOOLCHAIN STAMP_UAPI STAMP_PACKAGES STAMP_SERVICES
export STAMP_ROOTFS STAMP_IMAGE
