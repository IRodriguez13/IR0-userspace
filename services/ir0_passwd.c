/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_passwd.c
 * Description: passwd(1) — set-user-ID helper that rewrites shadow(5) hashes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ir0_auth.h"

static void out(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

/*
 * Batch mode: with a non-tty stdin the tool reads "old\nnew\nconfirm\n" (only
 * "new\nconfirm\n" for root), which is what the passwd smoke drives through a
 * pipe. Interactive use still prompts on the console.
 */
static int prompt(const char *label, char *buf, size_t len, int interactive)
{
	if (interactive)
		out(label);
	if (ir0_read_line(buf, len, 0) != 0)
		return -1;
	if (interactive)
		out("\n");
	return 0;
}

static int deny(const char *user, const char *why)
{
	char line[320];

	snprintf(line, sizeof(line), "passwd: %s\n[AUTH] passwd denied user=%s reason=%s\n",
		 why, user, why);
	out(line);
	return 1;
}

int main(int argc, char **argv)
{
	struct ir0_account target;
	struct ir0_account caller;
	char old_pw[IR0_AUTH_HASH_MAX];
	char new_pw[IR0_AUTH_HASH_MAX];
	char confirm[IR0_AUTH_HASH_MAX];
	char hash[IR0_AUTH_HASH_MAX];
	char stored[IR0_AUTH_HASH_MAX];
	char line[320];
	uid_t ruid = getuid();
	int interactive = isatty(0);
	int rc = 1;

	if (ir0_account_by_uid(ruid, &caller) != 0)
	{
		out("passwd: unknown caller\n");
		return 1;
	}

	if (argc > 1 && argv[1][0])
	{
		if (ir0_account_by_name(argv[1], &target) != 0)
		{
			out("passwd: unknown user\n");
			return 1;
		}
	}
	else
	{
		target = caller;
	}

	if (ruid != 0 && target.uid != ruid)
		return deny(caller.name, "not-permitted");

	if (ruid != 0)
	{
		const char *cur = target.passwd;

		if (strcmp(target.passwd, "x") == 0 ||
		    strcmp(target.passwd, "*") == 0)
		{
			if (ir0_shadow_hash(target.name, stored,
					    sizeof(stored)) != 0)
				return deny(caller.name, "no-shadow-entry");
			cur = stored;
		}
		if (prompt("Current password: ", old_pw, sizeof(old_pw),
			   interactive) != 0)
			return deny(caller.name, "read-error");
		if (!ir0_password_verify(cur, old_pw))
		{
			ir0_wipe(old_pw, sizeof(old_pw));
			return deny(caller.name, "authentication-failure");
		}
		ir0_wipe(old_pw, sizeof(old_pw));
	}

	snprintf(line, sizeof(line), "Changing password for %s\n", target.name);
	if (interactive)
		out(line);

	if (prompt("New password: ", new_pw, sizeof(new_pw), interactive) != 0 ||
	    prompt("Retype new password: ", confirm, sizeof(confirm),
		   interactive) != 0)
	{
		rc = deny(caller.name, "read-error");
		goto out_wipe;
	}

	if (strcmp(new_pw, confirm) != 0)
	{
		rc = deny(caller.name, "passwords-mismatch");
		goto out_wipe;
	}
	if (new_pw[0] == '\0')
	{
		rc = deny(caller.name, "empty-password");
		goto out_wipe;
	}

	if (ir0_password_hash(new_pw, hash, sizeof(hash)) != 0)
	{
		rc = deny(caller.name, "hash-error");
		goto out_wipe;
	}
	if (ir0_shadow_set_hash(target.name, hash) != 0)
	{
		rc = deny(caller.name, "shadow-write-error");
		goto out_wipe;
	}

	snprintf(line, sizeof(line),
		 "passwd: password updated successfully\n"
		 "[AUTH] passwd changed user=%s target=%s\n",
		 caller.name, target.name);
	out(line);
	rc = 0;

out_wipe:
	ir0_wipe(new_pw, sizeof(new_pw));
	ir0_wipe(confirm, sizeof(confirm));
	ir0_wipe(stored, sizeof(stored));
	return rc;
}
