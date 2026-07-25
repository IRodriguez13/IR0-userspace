/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_profile.h
 * Description: Product profile (/etc/ir0-profile) shared by firstboot and the
 * console service: development (root autologin lab), desktop (getty + login),
 * appliance (services only, no interactive login).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdio.h>
#include <string.h>

#define IR0_PROFILE_FILE "/etc/ir0-profile"

enum ir0_product_profile
{
	PROFILE_DEVELOPMENT = 0,
	PROFILE_DESKTOP,
	PROFILE_APPLIANCE
};

static inline enum ir0_product_profile ir0_read_profile(void)
{
	FILE *f;
	char line[64];

	f = fopen(IR0_PROFILE_FILE, "r");
	if (!f)
		return PROFILE_DESKTOP;
	if (!fgets(line, sizeof(line), f))
	{
		fclose(f);
		return PROFILE_DESKTOP;
	}
	fclose(f);
	if (strncmp(line, "development", 11) == 0)
		return PROFILE_DEVELOPMENT;
	if (strncmp(line, "appliance", 9) == 0)
		return PROFILE_APPLIANCE;
	return PROFILE_DESKTOP;
}
