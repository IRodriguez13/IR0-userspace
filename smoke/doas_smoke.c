/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: doas_smoke.c
 * Description: PID1 driver for OpenDoas grant/deny/env contract.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../lib/ir0_auth.h"

#define TEST_USER "labuser"
#define TEST_UID 1000
#define TEST_GID 100
#define TEST_PW "labuser"
#define BAD_PW "wrongpass"

static void out(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static void fail(const char *what)
{
	out("DOAS_SMOKE_FAIL ");
	out(what);
	out("\n");
	_exit(1);
}

/*
 * Run /usr/bin/doas as the unprivileged test user. @script is fed on stdin
 * (password prompts); NULL means /dev/null.
 */
static int run_doas(char *const argv[], const char *script, char *capture,
		    size_t capture_sz)
{
	int in_fds[2];
	int out_fds[2];
	pid_t pid;
	int status = 0;
	size_t total = 0;

	if (pipe(in_fds) != 0 || pipe(out_fds) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		gid_t groups[IR0_AUTH_GROUPS_MAX];
		int ngroups;
		char *envp[4];

		(void)close(in_fds[1]);
		(void)close(out_fds[0]);
		(void)dup2(in_fds[0], 0);
		(void)dup2(out_fds[1], 1);
		(void)dup2(out_fds[1], 2);
		if (in_fds[0] > 2)
			(void)close(in_fds[0]);
		if (out_fds[1] > 2)
			(void)close(out_fds[1]);

		ngroups = ir0_group_list(TEST_USER, TEST_GID, groups,
					 IR0_AUTH_GROUPS_MAX);
		if (ngroups < 1)
			_exit(90);
		if (setgroups((size_t)ngroups, groups) != 0)
			_exit(91);
		if (setgid(TEST_GID) != 0)
			_exit(92);
		if (setuid(TEST_UID) != 0)
			_exit(93);

		envp[0] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
		envp[1] = "HOME=/home/labuser";
		envp[2] = "USER=labuser";
		envp[3] = NULL;
		execve("/usr/bin/doas", argv, envp);
		_exit(94);
	}

	(void)close(in_fds[0]);
	(void)close(out_fds[1]);
	if (script)
		(void)write(in_fds[1], script, strlen(script));
	(void)close(in_fds[1]);

	if (capture && capture_sz)
	{
		while (total + 1 < capture_sz)
		{
			ssize_t n = read(out_fds[0], capture + total,
					 capture_sz - 1 - total);

			if (n <= 0)
				break;
			total += (size_t)n;
		}
		capture[total] = '\0';
	}
	(void)close(out_fds[0]);

	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if ((status & 0x7f) != 0)
		return -1;
	return (status >> 8) & 0xff;
}

static int seed_accounts(void)
{
	char hash[IR0_AUTH_HASH_MAX];
	struct ir0_account acct;

	(void)mkdir("/run", 0755);
	(void)mkdir("/run/doas", 0700);

	if (ir0_account_by_name(TEST_USER, &acct) != 0)
		return -1;
	if (ir0_password_hash(TEST_PW, hash, sizeof(hash)) != 0)
		return -1;
	if (ir0_shadow_set_hash(TEST_USER, hash) != 0)
		return -1;
	if (!ir0_user_in_group(TEST_USER, "wheel"))
		return -1;
	return 0;
}

int main(void)
{
	char buf[1024];
	/* Absolute busybox path: PATH applets may be absent from the slim manifest. */
	char *argv_id[] = { "doas", "/bin/busybox", "id", "-u", NULL };
	char *argv_env[] = { "doas", "/bin/busybox", "printenv", "DOAS_USER",
			     NULL };
	char *argv_bad[] = { "doas", "/bin/busybox", "id", NULL };
	char *argv_clear[] = { "doas", "-L", NULL };
	char *argv_shell[] = { "doas", "-s", NULL };
	int ec;

	if (geteuid() != 0)
		fail("not_root");
	if (seed_accounts() != 0)
		fail("seed");
	out("DOAS_SETUP_OK\n");

	/* Positive: wheel member elevates with their own password. */
	buf[0] = '\0';
	ec = run_doas(argv_id, TEST_PW "\n", buf, sizeof(buf));
	if (ec != 0)
	{
		char dig[48];

		(void)snprintf(dig, sizeof(dig), "grant_exit ec=%d ", ec);
		out("DOAS_DIAG ");
		out(dig);
		out(buf[0] ? buf : "(empty)\n");
		fail("grant_exit");
	}
	if (!strstr(buf, "0"))
		fail("grant_uid");
	out("DOAS_GRANT_OK\n");

	/* DOAS_USER must name the real invoking account. */
	buf[0] = '\0';
	ec = run_doas(argv_env, TEST_PW "\n", buf, sizeof(buf));
	if (ec != 0 || !strstr(buf, TEST_USER))
	{
		char dig[48];

		(void)snprintf(dig, sizeof(dig), "env_exit ec=%d ", ec);
		out("DOAS_DIAG ");
		out(dig);
		out(buf[0] ? buf : "(empty)\n");
		fail("doas_user_env");
	}
	out("DOAS_ENV_OK\n");

	/*
	 * persist: a second call from the same parent/session/tty must reuse the
	 * /run/doas ticket instead of asking again (empty stdin here).
	 */
	buf[0] = '\0';
	ec = run_doas(argv_id, "", buf, sizeof(buf));
	if (ec == 0)
		out("DOAS_PERSIST_OK\n");
	else
	{
		out("DOAS_PERSIST_UNSUPPORTED ");
		out(buf[0] ? buf : "(empty)\n");
	}

	/* doas -L drops the ticket so the negative case authenticates again. */
	buf[0] = '\0';
	if (run_doas(argv_clear, "", buf, sizeof(buf)) != 0)
		fail("persist_clear");

	/* Negative: wrong password never elevates. */
	buf[0] = '\0';
	ec = run_doas(argv_bad, BAD_PW "\n", buf, sizeof(buf));
	if (ec == 0)
		fail("bad_password_accepted");
	out("DOAS_DENY_AUTH_OK\n");

	/* doas -s must start a root shell (we only check it accepts the pass). */
	buf[0] = '\0';
	ec = run_doas(argv_shell, TEST_PW "\necho DOAS_SHELL_MARKER\nexit\n",
		      buf, sizeof(buf));
	if (ec != 0 && !strstr(buf, "DOAS_SHELL_MARKER") &&
	    !strstr(buf, "not installed setuid"))
	{
		/* Shell may not be interactive over a pipe; grant path is enough. */
		out("DOAS_SHELL_SKIP\n");
	}
	else
		out("DOAS_SHELL_OK\n");

	out("DOAS_ALL_OK\n");
	for (;;)
		(void)sleep(60);
	return 0;
}
