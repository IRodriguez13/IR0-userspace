/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_adduser.c
 * Description: adduser(8) — create a local Unix login (root only).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ir0_auth.h"

static void out(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static void err(const char *s)
{
	if (s)
		(void)write(2, s, strlen(s));
}

static int valid_username(const char *user)
{
	size_t i, n;

	if (!user || !user[0])
		return 0;
	n = strlen(user);
	if (n < 2 || n >= IR0_AUTH_NAME_MAX)
		return 0;
	if (!islower((unsigned char)user[0]) && user[0] != '_')
		return 0;
	for (i = 0; i < n; i++)
	{
		char c = user[i];

		if (!(islower((unsigned char)c) || isdigit((unsigned char)c) ||
		      c == '_' || c == '-'))
			return 0;
	}
	if (strcmp(user, "root") == 0 || strcmp(user, "bin") == 0 ||
	    strcmp(user, "daemon") == 0 || strcmp(user, "nobody") == 0)
		return 0;
	return 1;
}

static uid_t next_uid(void)
{
	FILE *f;
	char line[512];
	uid_t next = 1000;

	f = fopen(IR0_PASSWD_FILE, "r");
	if (!f)
		return next;
	while (fgets(line, sizeof(line), f))
	{
		char uid_s[32];
		uid_t u;

		if (ir0_auth_field(line, 2, uid_s, sizeof(uid_s)) != 0)
			continue;
		u = (uid_t)strtoul(uid_s, NULL, 10);
		if (u >= next)
			next = u + 1;
	}
	fclose(f);
	return next;
}

static int append_line(const char *path, mode_t mode, const char *line)
{
	int fd;
	size_t n;
	const char *p;

	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, mode);
	if (fd < 0)
		return -1;
	p = line;
	n = strlen(line);
	while (n > 0)
	{
		ssize_t w = write(fd, p, n);

		if (w <= 0)
		{
			(void)close(fd);
			return -1;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
	(void)close(fd);
	(void)chmod(path, mode);
	return 0;
}

static int ensure_group_member(const char *group, gid_t gid, const char *user)
{
	FILE *src;
	FILE *dst;
	char line[512];
	char tmp[] = "/etc/group.new";
	int found = 0;
	int has_user = 0;

	src = fopen(IR0_GROUP_FILE, "r");
	dst = fopen(tmp, "w");
	if (!dst)
	{
		if (src)
			fclose(src);
		return -1;
	}
	(void)chmod(tmp, 0644);

	if (src)
	{
		while (fgets(line, sizeof(line), src))
		{
			char name[IR0_AUTH_NAME_MAX];
			char members[256];
			char gid_s[32];
			char rest[8];

			if (ir0_auth_field(line, 0, name, sizeof(name)) != 0 ||
			    strcmp(name, group) != 0)
			{
				if (fputs(line, dst) == EOF)
					goto fail;
				continue;
			}
			found = 1;
			if (ir0_auth_field(line, 3, members, sizeof(members)) != 0)
				members[0] = '\0';
			if (ir0_auth_field(line, 1, rest, sizeof(rest)) != 0)
				snprintf(rest, sizeof(rest), "x");
			if (ir0_auth_field(line, 2, gid_s, sizeof(gid_s)) != 0)
				snprintf(gid_s, sizeof(gid_s), "%u", (unsigned)gid);
			if (members[0] && strstr(members, user))
				has_user = 1;
			if (has_user)
			{
				if (fputs(line, dst) == EOF)
					goto fail;
			}
			else if (members[0])
			{
				if (fprintf(dst, "%s:%s:%s:%s,%s\n", name, rest,
					    gid_s, members, user) < 0)
					goto fail;
			}
			else
			{
				if (fprintf(dst, "%s:%s:%s:%s\n", name, rest,
					    gid_s, user) < 0)
					goto fail;
			}
		}
		fclose(src);
		src = NULL;
	}

	if (!found)
	{
		if (fprintf(dst, "%s:x:%u:%s\n", group, (unsigned)gid, user) < 0)
			goto fail;
	}

	if (fflush(dst) != 0)
		goto fail;
	fclose(dst);
	if (rename(tmp, IR0_GROUP_FILE) != 0)
	{
		(void)unlink(tmp);
		return -1;
	}
	return 0;

fail:
	if (src)
		fclose(src);
	fclose(dst);
	(void)unlink(tmp);
	return -1;
}

int main(int argc, char **argv)
{
	struct ir0_account existing;
	char user[IR0_AUTH_NAME_MAX];
	char pw1[IR0_AUTH_HASH_MAX];
	char pw2[IR0_AUTH_HASH_MAX];
	char hash[IR0_AUTH_HASH_MAX];
	char line[320];
	char home[IR0_AUTH_PATH_MAX];
	uid_t uid;
	gid_t gid = 100;
	int interactive = isatty(0);
	int wheel = 1;

	if (geteuid() != 0)
	{
		err("adduser: must be root\n");
		return 1;
	}

	if (argc > 1 && argv[1][0])
		snprintf(user, sizeof(user), "%s", argv[1]);
	else
	{
		out("Enter your Unix username: ");
		if (ir0_read_line(user, sizeof(user), 1) != 0 || user[0] == '\0')
		{
			err("adduser: username required\n");
			return 1;
		}
	}

	if (!valid_username(user))
	{
		err("adduser: invalid or reserved username\n");
		return 1;
	}
	if (ir0_account_by_name(user, &existing) == 0)
	{
		err("adduser: user already exists\n");
		return 1;
	}

	if (interactive)
		out("New password: ");
	if (ir0_read_line(pw1, sizeof(pw1), 0) != 0)
	{
		err("adduser: read error\n");
		return 1;
	}
	if (interactive)
		out("\nRetype new password: ");
	if (ir0_read_line(pw2, sizeof(pw2), 0) != 0)
	{
		ir0_wipe(pw1, sizeof(pw1));
		err("adduser: read error\n");
		return 1;
	}
	if (interactive)
		out("\n");
	if (strcmp(pw1, pw2) != 0 || pw1[0] == '\0')
	{
		ir0_wipe(pw1, sizeof(pw1));
		ir0_wipe(pw2, sizeof(pw2));
		err("adduser: passwords empty or do not match\n");
		return 1;
	}
	if (ir0_password_hash(pw1, hash, sizeof(hash)) != 0)
	{
		ir0_wipe(pw1, sizeof(pw1));
		ir0_wipe(pw2, sizeof(pw2));
		err("adduser: hash error\n");
		return 1;
	}
	ir0_wipe(pw1, sizeof(pw1));
	ir0_wipe(pw2, sizeof(pw2));

	uid = next_uid();
	snprintf(home, sizeof(home), "/home/%s", user);
	(void)mkdir("/home", 0755);
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
	{
		err("adduser: cannot create home\n");
		return 1;
	}
	(void)chown(home, uid, gid);

	snprintf(line, sizeof(line),
		 "%s:x:%u:%u:%s:%s:/bin/sh\n", user, (unsigned)uid,
		 (unsigned)gid, user, home);
	if (append_line(IR0_PASSWD_FILE, 0644, line) != 0)
	{
		err("adduser: cannot update passwd\n");
		return 1;
	}
	snprintf(line, sizeof(line), "%s:%s:0:0:99999:7:::\n", user, hash);
	if (append_line(IR0_SHADOW_FILE, 0600, line) != 0)
	{
		err("adduser: cannot update shadow\n");
		return 1;
	}
	if (ensure_group_member("users", gid, user) != 0)
	{
		err("adduser: cannot update group\n");
		return 1;
	}
	if (wheel && ensure_group_member("wheel", 10, user) != 0)
	{
		err("adduser: cannot update wheel\n");
		return 1;
	}

	snprintf(line, sizeof(line),
		 "adduser: account '%s' created (uid %u)\n", user,
		 (unsigned)uid);
	out(line);
	return 0;
}
