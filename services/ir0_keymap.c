/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_keymap.c
 * Description: keymap CLI — switch IR0 console layout (us|latam).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ir0_keymap.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
	fputs("Usage: keymap [us|latam]\n"
	      "  (no args)  print current layout\n"
	      "  us|latam   set layout and write /etc/keymap\n",
	      stderr);
}

int main(int argc, char **argv)
{
	int layout;
	int cur;

	if (argc > 2)
	{
		usage();
		return 2;
	}
	if (argc == 2)
	{
		if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
		{
			usage();
			return 0;
		}
		layout = ir0_keymap_parse(argv[1]);
		if (layout < 0)
		{
			fputs("keymap: unknown layout (use us or latam)\n", stderr);
			return 1;
		}
		if (ir0_keymap_set(layout) != 0)
		{
			perror("keymap: keymap_set");
			return 1;
		}
		if (ir0_keymap_write_file(IR0_KEYMAP_FILE, layout) != 0)
		{
			perror("keymap: write /etc/keymap");
			return 1;
		}
		printf("%s\n", ir0_keymap_name(layout));
		return 0;
	}

	cur = ir0_keymap_get();
	if (cur < 0)
	{
		perror("keymap: keymap_get");
		return 1;
	}
	printf("%s\n", ir0_keymap_name(cur));
	return 0;
}
