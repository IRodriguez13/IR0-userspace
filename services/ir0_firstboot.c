/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_firstboot.c
 * Description: First-boot wizard / seed — no implicit personal identity.
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
#include "ir0_profile.h"
#include "ir0_smoke_tag.h"

#define ROOT_DENY_FILE "/etc/ir0-noroot"
#define DONE_FILE_ETC "/etc/ir0-firstboot-done"
#define DONE_FILE_VAR "/var/lib/ir0/firstboot.done"
#define RECOVERY_FLAG "/etc/ir0-recovery-enabled"
#define SEED_ENV "FIRSTBOOT_SEED"

static int write_file(const char *path, const char *data, mode_t mode)
{
	char tmp[256];
	int fd;
	size_t n;
	const char *p;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0)
		return -1;
	p = data;
	n = strlen(data);
	while (n > 0)
	{
		ssize_t w = write(fd, p, n);

		if (w <= 0)
		{
			(void)close(fd);
			(void)unlink(tmp);
			return -1;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
#if defined(__linux__) || defined(__IR0__)
	(void)fsync(fd);
#endif
	(void)close(fd);
	(void)chmod(tmp, mode);
	if (rename(tmp, path) != 0)
	{
		(void)unlink(tmp);
		return -1;
	}
	return 0;
}

static int mark_done(void)
{
	(void)mkdir("/var/lib", 0755);
	(void)mkdir("/var/lib/ir0", 0755);
	if (write_file(DONE_FILE_ETC, "ok\n", 0644) != 0)
		return -1;
	return write_file(DONE_FILE_VAR, "ok\n", 0644);
}

static int already_done(void)
{
	struct stat st;

	if (stat(DONE_FILE_VAR, &st) == 0 && S_ISREG(st.st_mode))
		return 1;
	if (stat(DONE_FILE_ETC, &st) == 0 && S_ISREG(st.st_mode))
		return 1;
	return 0;
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
	if (strcmp(user, "root") == 0 || strcmp(user, "ivan") == 0 ||
	    strcmp(user, "bin") == 0 || strcmp(user, "daemon") == 0 ||
	    strcmp(user, "nobody") == 0)
		return 0;
	return 1;
}

static int prompt_yes_no(const char *q, int def_yes)
{
	char buf[16];

	for (;;)
	{
		printf("%s [%s]: ", q, def_yes ? "Y/n" : "y/N");
		fflush(stdout);
		if (ir0_read_line(buf, sizeof(buf), 1) != 0 || buf[0] == '\0')
			return def_yes;
		if (buf[0] == 'y' || buf[0] == 'Y')
			return 1;
		if (buf[0] == 'n' || buf[0] == 'N')
			return 0;
	}
}

static int write_accounts(const char *user, const char *host, const char *hash,
			  int wheel, int lock_root, int recovery)
{
	char passwd[256];
	char shadow[320];
	char group[192];
	char home[128];
	char skel_src[64];
	char skel_dst[128];

	snprintf(passwd, sizeof(passwd),
		 "root:x:0:0:root:/root:/bin/sh\n"
		 "%s:x:1000:100:%s:/home/%s:/bin/sh\n",
		 user, user, user);
	snprintf(shadow, sizeof(shadow),
		 "root:%s:0:0:99999:7:::\n"
		 "%s:%s:0:0:99999:7:::\n",
		 lock_root ? "!" : hash, user, hash);
	if (wheel)
		snprintf(group, sizeof(group),
			 "root:x:0:\n"
			 "wheel:x:10:%s\n"
			 "users:x:100:%s\n",
			 user, user);
	else
		snprintf(group, sizeof(group),
			 "root:x:0:\n"
			 "users:x:100:%s\n",
			 user);

	(void)mkdir("/etc", 0755);
	(void)mkdir("/root", 0755);
	(void)mkdir("/home", 0755);
	snprintf(home, sizeof(home), "/home/%s", user);
	(void)mkdir(home, 0700);
	(void)chown(home, 1000, 100);
	/* Copy /etc/skel if present */
	snprintf(skel_src, sizeof(skel_src), "/etc/skel/.profile");
	if (access(skel_src, R_OK) == 0)
	{
		snprintf(skel_dst, sizeof(skel_dst), "%s/.profile", home);
		/* best-effort */
		(void)link(skel_src, skel_dst);
	}

	if (write_file("/etc/passwd", passwd, 0644) != 0)
		return -1;
	if (write_file("/etc/shadow", shadow, 0600) != 0)
		return -1;
	if (write_file("/etc/group", group, 0644) != 0)
		return -1;
	{
		char hostline[80];

		snprintf(hostline, sizeof(hostline), "%s\n", host);
		if (write_file("/etc/hostname", hostline, 0644) != 0)
			return -1;
	}

	if (lock_root)
		(void)write_file(ROOT_DENY_FILE, "1\n", 0644);
	else
		(void)unlink(ROOT_DENY_FILE);

	if (recovery)
		(void)write_file(RECOVERY_FLAG, "1\n", 0644);
	else
		(void)unlink(RECOVERY_FLAG);

	(void)unlink("/etc/ir0-autologin");
	return mark_done();
}

static int seed_development(void)
{
	puts("\n*** IR0 Development profile — NOT for production ***\n"
	     "root autologin / empty password allowed (labuser fixture).\n");
	if (write_accounts("labuser", "ir0", "", 1, 0, 1) != 0)
		return -1;
	(void)write_file("/etc/ir0-autologin", "root\n", 0644);
	(void)write_file("/etc/shadow",
			 "root::0:0:99999:7:::\n"
			 "labuser::0:0:99999:7:::\n",
			 0600);
	return 0;
}

static int seed_appliance(void)
{
	(void)mkdir("/etc", 0755);
	(void)mkdir("/root", 0755);
	if (write_file("/etc/passwd", "root:x:0:0:root:/root:/bin/sh\n",
		       0644) != 0)
		return -1;
	if (write_file("/etc/shadow", "root:!:0:0:99999:7:::\n", 0600) != 0)
		return -1;
	if (write_file("/etc/group", "root:x:0:\n", 0644) != 0)
		return -1;
	(void)write_file("/etc/hostname", "ir0\n", 0644);
	(void)write_file(ROOT_DENY_FILE, "1\n", 0644);
	(void)unlink("/etc/ir0-autologin");
	return mark_done();
}

static int apply_seed_file(const char *path)
{
	FILE *f;
	char line[256];
	char user[IR0_AUTH_NAME_MAX] = "";
	char host[64] = "ir0";
	char hash[IR0_AUTH_HASH_MAX] = "";
	int wheel = 1;
	int lock_root = 1;
	int recovery = 1;

	f = fopen(path, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
	{
		char *eq;

		if (line[0] == '#' || line[0] == '\n')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		eq++;
		eq[strcspn(eq, "\r\n")] = '\0';
		if (strcmp(line, "username") == 0)
			snprintf(user, sizeof(user), "%s", eq);
		else if (strcmp(line, "hostname") == 0)
			snprintf(host, sizeof(host), "%s", eq);
		else if (strcmp(line, "password_hash") == 0)
			snprintf(hash, sizeof(hash), "%s", eq);
		else if (strcmp(line, "wheel") == 0)
			wheel = (eq[0] == '1' || eq[0] == 'y' || eq[0] == 'Y');
		else if (strcmp(line, "lock_root") == 0)
			lock_root = (eq[0] == '1' || eq[0] == 'y' || eq[0] == 'Y');
		else if (strcmp(line, "recovery") == 0)
			recovery = (eq[0] == '1' || eq[0] == 'y' || eq[0] == 'Y');
	}
	fclose(f);
	if (!valid_username(user) || hash[0] == '\0')
	{
		fprintf(stderr, "firstboot: invalid FIRSTBOOT_SEED (need username + password_hash)\n");
		return -1;
	}
	return write_accounts(user, host, hash, wheel, lock_root, recovery);
}

static int wizard_interactive(void)
{
	char user[IR0_AUTH_NAME_MAX];
	char host[64];
	char pw1[IR0_AUTH_HASH_MAX];
	char pw2[IR0_AUTH_HASH_MAX];
	char hash[IR0_AUTH_HASH_MAX];
	int wheel;
	int lock_root;
	int recovery;

	puts("\nWelcome to IR0/Unix\n");
	puts("Create the first IR0/Unix user\n");

	for (;;)
	{
		printf("Username: ");
		fflush(stdout);
		if (ir0_read_line(user, sizeof(user), 1) != 0 || user[0] == '\0')
		{
			puts("Username required (no default).");
			continue;
		}
		if (!valid_username(user))
		{
			puts("Invalid or reserved username; try again.");
			continue;
		}
		break;
	}

	printf("Hostname [ir0]: ");
	fflush(stdout);
	if (ir0_read_line(host, sizeof(host), 1) != 0 || host[0] == '\0')
		snprintf(host, sizeof(host), "ir0");

	for (;;)
	{
		printf("Password: ");
		fflush(stdout);
		(void)ir0_read_line(pw1, sizeof(pw1), 0);
		puts("");
		printf("Confirm password: ");
		fflush(stdout);
		(void)ir0_read_line(pw2, sizeof(pw2), 0);
		puts("");
		if (strcmp(pw1, pw2) == 0 && pw1[0] != '\0')
			break;
		puts("Passwords empty or do not match; try again.");
		ir0_wipe(pw1, sizeof(pw1));
		ir0_wipe(pw2, sizeof(pw2));
	}

	wheel = prompt_yes_no("Grant administrative privileges (wheel)?", 1);
	lock_root = prompt_yes_no("Disable direct root login?", 1);
	recovery = prompt_yes_no("Enable local recovery mode?", 1);

	if (ir0_password_hash(pw1, hash, sizeof(hash)) != 0)
	{
		ir0_wipe(pw1, sizeof(pw1));
		ir0_wipe(pw2, sizeof(pw2));
		return -1;
	}
	ir0_wipe(pw1, sizeof(pw1));
	ir0_wipe(pw2, sizeof(pw2));
	return write_accounts(user, host, hash, wheel, lock_root, recovery);
}

int main(void)
{
	enum ir0_product_profile profile;
	int interactive;
	const char *seed;

	if (already_done())
	{
		ir0_smoke_tag("FIRSTBOOT_SKIP\n");
		return 0;
	}

	profile = ir0_read_profile();
	interactive = isatty(0) && isatty(1);
	seed = getenv(SEED_ENV);

	if (profile == PROFILE_DEVELOPMENT)
	{
		if (seed && seed[0])
		{
			if (apply_seed_file(seed) != 0)
				goto fail;
		}
		else if (seed_development() != 0)
			goto fail;
		ir0_smoke_tag("FIRSTBOOT_DEV_OK\n");
		return 0;
	}
	if (profile == PROFILE_APPLIANCE)
	{
		if (seed_appliance() != 0)
			goto fail;
		ir0_smoke_tag("FIRSTBOOT_APPLIANCE_OK\n");
		return 0;
	}

	/* minimal + desktop */
	if (seed && seed[0])
	{
		if (apply_seed_file(seed) != 0)
			goto fail;
		ir0_smoke_tag("FIRSTBOOT_OK\n");
		return 0;
	}
	if (interactive)
	{
		if (wizard_interactive() != 0)
			goto fail;
		ir0_smoke_tag("FIRSTBOOT_OK\n");
		return 0;
	}

	fprintf(stderr,
		"firstboot: no TTY and no %s — leaving firstboot pending\n",
		SEED_ENV);
	ir0_smoke_tag("FIRSTBOOT_PENDING\n");
	return 0;

fail:
	ir0_smoke_tag("FIRSTBOOT_FAIL\n");
	return 1;
}
