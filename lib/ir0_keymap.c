/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_keymap.c
 * Description: keymap_set/get wrappers + /etc/keymap persistence.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ir0_keymap.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ir0/syscall_linux.h>

const char *ir0_keymap_name(int layout)
{
	if (layout == IR0_KBD_LAYOUT_LATAM)
		return "latam";
	if (layout == IR0_KBD_LAYOUT_US)
		return "us";
	return "unknown";
}

int ir0_keymap_parse(const char *s)
{
	char buf[32];
	size_t n;

	if (!s)
		return -1;
	while (*s && isspace((unsigned char)*s))
		s++;
	n = 0;
	while (s[n] && !isspace((unsigned char)s[n]) && n + 1 < sizeof(buf))
	{
		buf[n] = (char)tolower((unsigned char)s[n]);
		n++;
	}
	buf[n] = '\0';
	if (n == 0)
		return -1;
	if (strcmp(buf, "us") == 0 || strcmp(buf, "0") == 0)
		return IR0_KBD_LAYOUT_US;
	if (strcmp(buf, "latam") == 0 || strcmp(buf, "la") == 0 ||
	    strcmp(buf, "es") == 0 || strcmp(buf, "1") == 0)
		return IR0_KBD_LAYOUT_LATAM;
	return -1;
}

int ir0_keymap_get(void)
{
	long r;

	r = syscall(__NR_keymap_get);
	if (r < 0)
		return -1;
	return (int)r;
}

int ir0_keymap_set(int layout)
{
	long r;

	if (layout != IR0_KBD_LAYOUT_US && layout != IR0_KBD_LAYOUT_LATAM)
	{
		errno = EINVAL;
		return -1;
	}
	r = syscall(__NR_keymap_set, (long)layout);
	if (r < 0)
		return -1;
	return 0;
}

int ir0_keymap_apply_file(const char *path)
{
	char line[64];
	FILE *f;
	int layout;

	if (!path)
		path = IR0_KEYMAP_FILE;
	f = fopen(path, "r");
	if (!f)
		return 1;
	if (!fgets(line, sizeof(line), f))
	{
		fclose(f);
		return -1;
	}
	fclose(f);
	layout = ir0_keymap_parse(line);
	if (layout < 0)
		return -1;
	if (ir0_keymap_set(layout) != 0)
		return -1;
	return 0;
}

int ir0_keymap_write_file(const char *path, int layout)
{
	const char *name;
	char tmp[sizeof(IR0_KEYMAP_FILE) + 8];
	int fd;
	ssize_t n;
	size_t len;

	if (!path)
		path = IR0_KEYMAP_FILE;
	name = ir0_keymap_name(layout);
	if (strcmp(name, "unknown") == 0)
	{
		errno = EINVAL;
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	len = strlen(name);
	n = write(fd, name, len);
	if (n == (ssize_t)len)
		n = write(fd, "\n", 1);
	if (close(fd) != 0 || n != 1)
	{
		(void)unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0)
	{
		(void)unlink(tmp);
		return -1;
	}
	return 0;
}
