/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_recovery.c
 * Description: Local recovery shell — UID 0, rootfs RO until remount.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ir0_smoke_tag.h"

/* musl mount(2) is the Linux 5-arg form; IR0 only consumes the first three. */
extern int mount(const char *source, const char *target,
		 const char *filesystemtype, unsigned long mountflags,
		 const void *data);

/*
 * IR0's mount(2) is still the 3-arg form. Remount is expressed as:
 *   mount("remount", "/", "ro", 0, NULL)  /  mount(..., "rw", ...)
 */
static int remount_root(const char *mode)
{
	return mount("remount", "/", mode, 0, NULL);
}

static void puts_fd(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

int main(int argc, char **argv)
{
	char *shell_argv[3];

	(void)argc;
	(void)argv;

	ir0_smoke_tag("RECOVERY_START\n");
	puts_fd("\nIR0/Unix Recovery Environment\n\n");
	puts_fd("Networking: disabled\n");
	puts_fd("Root filesystem: read-only\n");
	puts_fd("Direct root login is disabled in normal mode.\n\n");
	puts_fd("Type 'mount-root-rw' to enable writes.\n\n");

	if (remount_root("ro") != 0)
	{
		ir0_smoke_tag("RECOVERY_REMOUNT_RO_FAIL\n");
		puts_fd("(warning: remount ro failed — continuing anyway)\n");
	}
	else
		ir0_smoke_tag("RECOVERY_ROOT_RO\n");

	ir0_smoke_tag("RECOVERY_UID=");
	{
		char buf[32];

		snprintf(buf, sizeof(buf), "%u\n", (unsigned)geteuid());
		ir0_smoke_tag(buf);
	}

	/* Interactive recovery shell as UID 0 (we are already root). */
	shell_argv[0] = "-sh";
	shell_argv[1] = "-i";
	shell_argv[2] = NULL;
	(void)setenv("HOME", "/root", 1);
	(void)setenv("USER", "root", 1);
	(void)setenv("HOSTNAME", "ir0-recovery", 1);
	(void)setenv("PS1", "root@ir0-recovery:/$PWD# ", 1);
	(void)setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
	ir0_smoke_tag("RECOVERY_SHELL_READY\n");
	execv("/bin/sh", shell_argv);
	ir0_smoke_tag("RECOVERY_EXEC_FAIL\n");
	return 111;
}
