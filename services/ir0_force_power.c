/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_force_power.c
 * Description: ELF stand-ins for halt/poweroff/reboot (force reboot(2)).
 *
 * BusyBox applets without -f only kill(1, SIG*) for SysV/busybox init.
 * Runit has no handlers; IR0 has no shebang loader yet — so product images
 * install this small ELF (hardlinked as halt/poweroff/reboot) instead.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/reboot.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_HALT
#define LINUX_REBOOT_CMD_HALT 0xCDEF0123u
#endif
#ifndef LINUX_REBOOT_CMD_POWER_OFF
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321FEDCu
#endif
#ifndef LINUX_REBOOT_CMD_RESTART
#define LINUX_REBOOT_CMD_RESTART 0x01234567u
#endif

static const char *base_name(const char *path)
{
	const char *s = path ? strrchr(path, '/') : NULL;

	return (s && s[1]) ? s + 1 : (path ? path : "");
}

int main(int argc, char **argv)
{
	unsigned int cmd = LINUX_REBOOT_CMD_POWER_OFF;
	const char *name = base_name(argc > 0 ? argv[0] : "poweroff");

	(void)argc;
	if (strcmp(name, "halt") == 0)
		cmd = LINUX_REBOOT_CMD_HALT;
	else if (strcmp(name, "reboot") == 0)
		cmd = LINUX_REBOOT_CMD_RESTART;
	else
		cmd = LINUX_REBOOT_CMD_POWER_OFF;

	(void)syscall(SYS_sync);
	(void)syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd,
		      (void *)0);
	return 1;
}
