/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fsck.ir0.c
 * Description: Minimal MINIX v1 superblock check for runit stage1 (honest tags).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "ir0_smoke_tag.h"

#define MINIX_V1_MAGIC 0x137F

static int try_dev(const char *path)
{
	int fd;
	unsigned char buf[32];
	uint16_t magic;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, 1024, SEEK_SET) < 0)
	{
		(void)close(fd);
		return -1;
	}
	if (read(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf))
	{
		(void)close(fd);
		return -1;
	}
	(void)close(fd);
	magic = (uint16_t)buf[16] | ((uint16_t)buf[17] << 8);
	if (magic != MINIX_V1_MAGIC)
		return -2;
	return 0;
}

int main(void)
{
	static const char *devs[] = {
		"/dev/hda", "/dev/sda", "/dev/root", "/dev/ata0", NULL
	};
	int i;
	int saw_open = 0;

	for (i = 0; devs[i]; i++)
	{
		int rc = try_dev(devs[i]);

		if (rc == -1)
			continue;
		saw_open = 1;
		if (rc == 0)
		{
			ir0_smoke_tag("FSCK_OK\n");
			return 0;
		}
		ir0_smoke_tag("FSCK_FAIL\n");
		return 1;
	}

	/* No usable block device node — do not claim clean. */
	(void)saw_open;
	ir0_smoke_tag("FSCK_SKIPPED\n");
	return 0;
}
