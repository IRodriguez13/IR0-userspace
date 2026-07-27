/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_stage1.c
 * Description: runit stage1 — fsck, firstboot, optional recovery handoff.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ir0_smoke_tag.h"

static int cmdline_has_recovery(void)
{
	FILE *f;
	char buf[256];

	f = fopen("/proc/cmdline", "r");
	if (!f)
		return 0;
	if (!fgets(buf, sizeof(buf), f))
	{
		fclose(f);
		return 0;
	}
	fclose(f);
	return strstr(buf, "ir0.recovery=1") != NULL;
}

static void run_helper(const char *path)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0)
	{
		char *const argv[] = { (char *)path, NULL };

		execv(path, argv);
		_exit(127);
	}
	(void)waitpid(pid, &status, 0);
}

/* Non-interactive firstboot only — wizard runs later on the console TTY. */
static void run_firstboot_early(void)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0)
	{
		char *const argv[] = { "/sbin/ir0-firstboot", "--early", NULL };

		execv(argv[0], argv);
		_exit(127);
	}
	(void)waitpid(pid, &status, 0);
}

static int want_fsck(void)
{
	/* Skip unconditional fsck when the profile opts out. */
	return access("/etc/ir0-skip-fsck", F_OK) != 0;
}

int main(void)
{
	char *const argv2[] = { "/etc/runit/2", NULL };
	char *const argv_rec[] = { "/sbin/ir0-recovery", NULL };

	if (want_fsck())
		run_helper("/sbin/fsck.ir0");
	run_firstboot_early();

	ir0_smoke_tag("RUNIT_STAGE1_OK\n");

	if (cmdline_has_recovery())
	{
		ir0_smoke_tag("RECOVERY_BOOT_SELECTED\n");
		execv("/sbin/ir0-recovery", argv_rec);
		ir0_smoke_tag("RECOVERY_HANDOFF_FAIL\n");
		return 111;
	}

	execv("/etc/runit/2", argv2);
	ir0_smoke_tag("RUNIT_STAGE1_EXEC_FAIL\n");
	return 111;
}
