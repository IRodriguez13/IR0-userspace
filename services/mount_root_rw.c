/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mount_root_rw.c
 * Description: Remount / read-write from the recovery environment.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ir0_smoke_tag.h"

extern int mount(const char *source, const char *target,
		 const char *filesystemtype, unsigned long mountflags,
		 const void *data);

int main(void)
{
	if (geteuid() != 0)
	{
		(void)write(2, "mount-root-rw: must be root\n", 28);
		return 1;
	}
	if (mount("remount", "/", "rw", 0, NULL) != 0)
	{
		ir0_smoke_tag("RECOVERY_REMOUNT_RW_FAIL\n");
		perror("mount-root-rw");
		return 1;
	}
	ir0_smoke_tag("RECOVERY_ROOT_RW\n");
	(void)write(1, "Root filesystem remounted read-write.\n", 38);
	return 0;
}
