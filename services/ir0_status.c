/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_status.c
 * Description: Userspace status helper — BusyBox applet matrix dump.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MATRIX_PATH "/etc/busybox/bb_status.tsv"

static void put(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static int dump_matrix(void)
{
	FILE *f;
	char line[256];
	int n = 0;

	f = fopen(MATRIX_PATH, "r");
	if (!f)
	{
		put("ir0-status busybox: missing ");
		put(MATRIX_PATH);
		put(" (run make busybox-matrix on the host)\n");
		return 1;
	}

	put("BusyBox applet status (supported / partial / unavailable):\n");
	while (fgets(line, sizeof(line), f))
	{
		char *applet;
		char *status;
		char *evidence;
		char *save;

		if (line[0] == '#' || line[0] == '\n')
			continue;
		applet = strtok_r(line, " \t\n", &save);
		status = strtok_r(NULL, " \t\n", &save);
		evidence = strtok_r(NULL, "\n", &save);
		if (!applet || !status)
			continue;
		while (evidence && (*evidence == ' ' || *evidence == '\t'))
			evidence++;
		put("  ");
		put(applet);
		put("  ");
		put(status);
		if (evidence && evidence[0])
		{
			put("  ");
			put(evidence);
		}
		put("\n");
		n++;
	}
	fclose(f);
	if (n == 0)
	{
		put("(empty matrix)\n");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "busybox") != 0)
	{
		put("usage: ir0-status busybox\n");
		return 1;
	}
	return dump_matrix();
}
