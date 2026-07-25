/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_firstboot.c
 * Description: First-boot account wizard (Desktop) and Development seed.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

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
#define DONE_FILE "/etc/ir0-firstboot-done"
#define RECOVERY_FLAG "/etc/ir0-recovery-enabled"

static int write_file(const char *path, const char *data, mode_t mode)
{
	int fd;
	size_t n;
	const char *p;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
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
			return -1;
		}
		p += (size_t)w;
		n -= (size_t)w;
	}
	(void)close(fd);
	(void)chmod(path, mode);
	return 0;
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
	char profile[512];

	snprintf(passwd, sizeof(passwd),
		 "root:x:0:0:root:/root:/bin/sh\n"
		 "%s:x:1000:100:%s:/home/%s:/bin/sh\n",
		 user, user, user);
	snprintf(shadow, sizeof(shadow),
		 "root:%s:0:0:99999:7:::\n"
		 "%s:%s:0:0:99999:7:::\n",
		 lock_root ? "!" : "", user, hash);
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
	snprintf(profile, sizeof(profile),
		 "export PATH=/bin:/sbin:/usr/bin:/usr/sbin\n"
		 "USER=\"${USER:-root}\"\n"
		 "HOSTNAME=\"${HOSTNAME:-%s}\"\n"
		 "case \"$USER\" in\n"
		 "root)\n"
		 "\tPS1=\"root@$HOSTNAME\"':$PWD# '\n"
		 "\t;;\n"
		 "*)\n"
		 "\tPS1=\"$USER@$HOSTNAME\"':$PWD$ '\n"
		 "\t;;\n"
		 "esac\n"
		 "export PS1 HOSTNAME USER\n",
		 host);
	if (write_file("/etc/profile", profile, 0644) != 0)
		return -1;
	(void)write_file("/etc/issue", "Unauthorized access prohibited.\n", 0644);
	(void)write_file("/etc/doas.conf",
			 "permit persist :wheel as root\n", 0440);

	if (lock_root)
		(void)write_file(ROOT_DENY_FILE, "1\n", 0644);
	else
		(void)unlink(ROOT_DENY_FILE);

	if (recovery)
		(void)write_file(RECOVERY_FLAG, "1\n", 0644);
	else
		(void)unlink(RECOVERY_FLAG);

	(void)write_file(DONE_FILE, "ok\n", 0644);
	return 0;
}

/* Non-interactive Desktop defaults for smokes / imaging. */
static int seed_desktop_batch(void)
{
	char hash[IR0_AUTH_HASH_MAX];

	/* Lab password "ivan" — images that ship a real firstboot omit this path. */
	if (ir0_password_hash("ivan", hash, sizeof(hash)) != 0)
		return -1;
	return write_accounts("ivan", "ir0", hash, 1, 1, 1);
}

static int seed_development(void)
{
	char hash[IR0_AUTH_HASH_MAX];

	puts("\n*** IR0 Development profile — NOT for production ***\n"
	     "root autologin / empty password allowed.\n");
	/* Empty password: shadow field "" for root and ivan (lab only). */
	hash[0] = '\0';
	if (write_accounts("ivan", "ir0", hash, 1, 0, 1) != 0)
		return -1;
	(void)write_file("/etc/ir0-autologin", "root\n", 0644);
	(void)write_file("/etc/shadow",
			 "root::0:0:99999:7:::\n"
			 "ivan::0:0:99999:7:::\n",
			 0600);
	return 0;
}

static int seed_appliance(void)
{
	/* No interactive login accounts — services only. */
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
	(void)write_file(DONE_FILE, "ok\n", 0644);
	(void)unlink("/etc/ir0-autologin");
	return 0;
}

static int wizard_desktop(void)
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

	printf("Username: ");
	fflush(stdout);
	if (ir0_read_line(user, sizeof(user), 1) != 0 || user[0] == '\0')
		snprintf(user, sizeof(user), "ivan");

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
		puts("Passwords do not match; try again.");
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

	(void)unlink("/etc/ir0-autologin");
	return write_accounts(user, host, hash, wheel, lock_root, recovery);
}

int main(void)
{
	struct stat st;
	enum ir0_product_profile profile;
	int interactive;

	if (stat("/etc/passwd", &st) == 0 && S_ISREG(st.st_mode) &&
	    st.st_size > 0)
	{
		ir0_smoke_tag("FIRSTBOOT_SKIP\n");
		return 0;
	}

	profile = ir0_read_profile();
	interactive = isatty(0) && isatty(1);

	if (profile == PROFILE_DEVELOPMENT)
	{
		if (seed_development() != 0)
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

	/* Desktop: interactive wizard, or batch seed when stdin is not a tty. */
	if (interactive)
	{
		if (wizard_desktop() != 0)
			goto fail;
	}
	else if (seed_desktop_batch() != 0)
		goto fail;

	ir0_smoke_tag("FIRSTBOOT_OK\n");
	return 0;

fail:
	ir0_smoke_tag("FIRSTBOOT_FAIL\n");
	return 1;
}
