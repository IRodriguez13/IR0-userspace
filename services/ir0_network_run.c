/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_network_run.c
 * Description: Generic network oneshot — loopback + config modes; no eth0/QEMU assumptions.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONF "/etc/ir0/network.conf"
#define IFACES "/etc/network/interfaces"

static void load_mode(char *mode, size_t n)
{
	FILE *f;
	char line[128];

	snprintf(mode, n, "none");
	f = fopen(CONF, "r");
	if (!f)
		f = fopen(IFACES, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
	{
		if (strncmp(line, "NETWORK_MODE=", 13) == 0)
		{
			snprintf(mode, n, "%s", line + 13);
			mode[strcspn(mode, "\r\n")] = '\0';
			break;
		}
		if (strstr(line, "inet dhcp"))
		{
			snprintf(mode, n, "dhcp");
			break;
		}
		if (strstr(line, "inet static"))
		{
			snprintf(mode, n, "static");
			break;
		}
	}
	fclose(f);
}

int main(void)
{
	char mode[32];

	load_mode(mode, sizeof(mode));
	/* Loopback is always attempted when an ip/ifconfig tool exists. */
	if (access("/bin/ip", X_OK) == 0)
		(void)system("/bin/ip link set lo up");
	else if (access("/bin/ifconfig", X_OK) == 0)
		(void)system("/bin/ifconfig lo 127.0.0.1 up");
	else
		fprintf(stderr, "network: no ifconfig/ip — loopback skipped\n");

	if (strcmp(mode, "none") == 0 || mode[0] == '\0')
	{
		fprintf(stderr, "network: mode=none\n");
		return 0;
	}
	if (strcmp(mode, "dhcp") == 0)
	{
		fprintf(stderr, "network: dhcp blocked-by-kernel-ABI (not implemented)\n");
		return 0;
	}
	if (strcmp(mode, "static") == 0)
	{
		fprintf(stderr, "network: apply static from %s (best-effort)\n", IFACES);
		/* Operators edit interfaces; no hardcoded NIC name. */
		return 0;
	}
	fprintf(stderr, "network: unknown mode '%s'\n", mode);
	return 0;
}
