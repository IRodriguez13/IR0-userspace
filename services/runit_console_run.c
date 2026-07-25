/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: runit_console_run.c
 * Description: runit console service — Unix getty/login, privilege drop, ash.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "ir0_auth.h"
#include "ir0_profile.h"
#include "ir0_smoke_tag.h"

/* Desktop profile marker: direct root login is refused before any password. */
#define ROOT_LOGIN_DENY_FILE "/etc/ir0-noroot"

static void puts_fd(const char *s)
{
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void strip_ws(char *s)
{
	char *p;

	if (!s)
		return;
	p = s;
	while (*p)
	{
		if (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')
		{
			*p = '\0';
			return;
		}
		p++;
	}
}

static void attach_console(void)
{
	int fd;

	fd = open("/dev/console", O_RDWR);
	if (fd < 0)
		return;
	(void)dup2(fd, 0);
	(void)dup2(fd, 1);
	(void)dup2(fd, 2);
	if (fd > 2)
		(void)close(fd);
}

/* Audit trail without secrets: user, tty and outcome only. */
static void audit(const char *what, const char *user)
{
	char line[160];

	snprintf(line, sizeof(line), "[AUTH] %s user=%s tty=console\n", what,
		 user && user[0] ? user : "?");
	puts_fd(line);
}

static int root_login_denied(const char *user)
{
	if (strcmp(user, "root") != 0)
		return 0;
	return access(ROOT_LOGIN_DENY_FILE, F_OK) == 0;
}

static int auth_user(const char *user, const char *password,
		     struct ir0_account *acct)
{
	char hash[IR0_AUTH_HASH_MAX];
	const char *stored;

	if (!user || !acct)
		return -1;
	if (ir0_account_by_name(user, acct) != 0)
		return -1;
	if (root_login_denied(user))
	{
		ir0_smoke_tag("LOGIN_ROOT_DENIED\n");
		audit("root login refused", user);
		return -1;
	}

	stored = acct->passwd;
	if (strcmp(acct->passwd, "x") == 0 || strcmp(acct->passwd, "*") == 0)
	{
		if (ir0_shadow_hash(user, hash, sizeof(hash)) != 0)
			return -1;
		stored = hash;
	}

	if (!ir0_password_verify(stored, password))
		return -1;
	return 0;
}

static int start_session(const struct ir0_account *acct)
{
	char *argv[3];
	gid_t groups[IR0_AUTH_GROUPS_MAX];
	int ngroups;
	char host[64];

	if (!acct)
		return -1;

	/* Supplementary groups (wheel and friends) must land before the drop. */
	ngroups = ir0_group_list(acct->name, acct->gid, groups,
				 IR0_AUTH_GROUPS_MAX);
	if (ngroups > 0)
		(void)setgroups((size_t)ngroups, groups);
	if (setgid(acct->gid) != 0)
		return -1;
	if (setuid(acct->uid) != 0)
		return -1;

	if (chdir(acct->home) != 0)
		(void)chdir("/");

	ir0_hostname(host, sizeof(host));
	(void)setenv("HOME", acct->home, 1);
	(void)setenv("USER", acct->name, 1);
	(void)setenv("LOGNAME", acct->name, 1);
	(void)setenv("SHELL", acct->shell, 1);
	(void)setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
	(void)setenv("HOSTNAME", host, 1);
	/*
	 * ncurses/nano are built with linux/vt100/xterm fallbacks only.
	 * Unset TERM defaults to vt220 inside ncurses → "Error opening terminal".
	 */
	(void)setenv("TERM", "linux", 1);

	{
		char tag[64];

		snprintf(tag, sizeof(tag), "LOGIN_UID=%u EUID=%u\n",
			 (unsigned)getuid(), (unsigned)geteuid());
		ir0_smoke_tag(tag);
	}

	/*
	 * Password entry may leave ECHO off if restore failed. Ash then looks
	 * dead (no echo, lines never finish). Force cooked+echo before exec.
	 */
	(void)ir0_tty_restore_cooked();

	/*
	 * argv[0] starts with '-' so ash treats this as a login shell and reads
	 * /etc/profile, which owns PS1 (no hardcoded prompt here).
	 */
	argv[0] = "-sh";
	argv[1] = "-i";
	argv[2] = NULL;
	execv(acct->shell[0] ? acct->shell : "/bin/sh", argv);
	execv("/bin/sh", argv);
	return -1;
}

static void print_issue(void)
{
	struct utsname u;
	FILE *f;
	char line[256];
	char banner[96];

	memset(&u, 0, sizeof(u));
	(void)uname(&u);
	snprintf(banner, sizeof(banner), "\nIR0/Unix %s\n",
		 u.release[0] ? u.release : "0.0.1");
	puts_fd(banner);

	f = fopen("/etc/issue", "r");
	if (f)
	{
		while (fgets(line, sizeof(line), f))
			puts_fd(line);
		fclose(f);
	}
}

static void read_virt_label(char *out, size_t outlen)
{
	FILE *f;
	char line[128];

	if (!out || outlen == 0)
		return;
	snprintf(out, outlen, "bare metal");
	f = fopen("/proc/cpuinfo", "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
	{
		if (strncmp(line, "hypervisor_vendor\t", 18) == 0)
		{
			char *v = line + 18;

			strip_ws(v);
			if (strcmp(v, "KVMKVMKVM") == 0 ||
			    strncmp(v, "TCGTCGTCG", 9) == 0 ||
			    strstr(v, "TCG") != NULL)
				snprintf(out, outlen, "QEMU TCG");
			else if (strstr(v, "KVM") != NULL)
				snprintf(out, outlen, "QEMU KVM");
			else if (v[0])
			{
				size_t n = 0;

				while (v[n] && n + 1 < outlen)
				{
					out[n] = v[n];
					n++;
				}
				out[n] = '\0';
			}
			else
				snprintf(out, outlen, "hypervisor");
			break;
		}
		if (strstr(line, "hypervisor") != NULL &&
		    strncmp(line, "flags\t", 6) == 0)
			snprintf(out, outlen, "QEMU TCG");
	}
	fclose(f);
}

/*
 * The whole banner goes out in a single write: other services and the kernel
 * log share this console, and a line-by-line banner interleaves with them.
 */
static void print_welcome(enum ir0_product_profile profile)
{
	struct utsname u;
	FILE *f;
	char uptime[64];
	char virt[48];
	char banner[640];
	char *dot;

	memset(&u, 0, sizeof(u));
	(void)uname(&u);
	read_virt_label(virt, sizeof(virt));

	uptime[0] = '\0';
	f = fopen("/proc/uptime", "r");
	if (f)
	{
		if (fgets(uptime, sizeof(uptime), f))
			strip_ws(uptime);
		fclose(f);
	}
	dot = strchr(uptime, '.');
	if (dot)
		*dot = '\0';

	snprintf(banner, sizeof(banner),
		 "\nWelcome to IR0/Unix\n"
		 "  Kernel:  %s %s\n"
		 "  Machine: %s · %s\n"
		 "  Uptime:  %s s\n"
		 "  Docs:    man IR0\n"
		 "  Status:  ir0-status\n"
		 "%s\n",
		 u.sysname[0] ? u.sysname : "IR0",
		 u.release[0] ? u.release : "?",
		 u.machine[0] ? u.machine : "?", virt,
		 uptime[0] ? uptime : "unknown",
		 profile == PROFILE_DEVELOPMENT
			 ? "\nIR0/Unix development environment\n"
			   "WARNING: automatic root login is enabled\n"
			 : "");
	puts_fd(banner);
}

int main(void)
{
	char user[IR0_AUTH_NAME_MAX];
	char pass[IR0_AUTH_HASH_MAX];
	char host[64];
	char prompt[96];
	struct ir0_account acct;
	enum ir0_product_profile profile;
	FILE *auto_f;

	ir0_smoke_tag("RUNSV_CONSOLE_START\n");
	attach_console();
	ir0_smoke_tag("GETTY_READY\n");

	profile = ir0_read_profile();
	if (profile == PROFILE_APPLIANCE)
	{
		/* Appliance images run services only: no getty, no shell. */
		ir0_smoke_tag("CONSOLE_NO_LOGIN\n");
		for (;;)
			(void)pause();
	}

	auto_f = fopen("/etc/ir0-autologin", "r");
	if (auto_f)
	{
		if (fgets(user, sizeof(user), auto_f))
			strip_ws(user);
		else
			user[0] = '\0';
		fclose(auto_f);
		pass[0] = '\0';
		if (user[0] && auth_user(user, pass, &acct) == 0)
		{
			ir0_smoke_tag("LOGIN_OK\n");
			audit("login granted", user);
			print_welcome(profile);
			ir0_smoke_tag("ASH_INTERACTIVE_READY\n");
			if (start_session(&acct) != 0)
			{
				ir0_smoke_tag("RUNSV_CONSOLE_EXEC_FAIL\n");
				return 111;
			}
		}
		ir0_smoke_tag("LOGIN_AUTO_FAIL\n");
	}

	/* Traditional Unix prompt: "<hostname> login:". */
	ir0_hostname(host, sizeof(host));
	snprintf(prompt, sizeof(prompt), "%s login: ", host[0] ? host : "ir0");

	for (;;)
	{
		print_issue();
		puts_fd(prompt);
		(void)ir0_read_line(user, sizeof(user), 1);
		strip_ws(user);
		ir0_smoke_tag("LOGIN_USER_READ\n");
		if (user[0] == '\0')
			continue;
		puts_fd("Password: ");
		(void)ir0_read_line(pass, sizeof(pass), 0);
		ir0_smoke_tag("LOGIN_PASS_READ\n");
		puts_fd("\n");

		if (auth_user(user, pass, &acct) != 0)
		{
			ir0_wipe(pass, sizeof(pass));
			puts_fd("Login incorrect\n");
			audit("login denied", user);
			sleep(1);
			continue;
		}
		ir0_wipe(pass, sizeof(pass));

		ir0_smoke_tag("LOGIN_OK\n");
		audit("login granted", user);
		print_welcome(profile);
		ir0_smoke_tag("ASH_INTERACTIVE_READY\n");
		if (start_session(&acct) != 0)
		{
			ir0_smoke_tag("RUNSV_CONSOLE_EXEC_FAIL\n");
			return 111;
		}
	}
}
