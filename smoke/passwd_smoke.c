/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: passwd_smoke.c
 * Description: PID1 driver for the passwd(1) shadow-update contract.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../lib/ir0_auth.h"

#define TEST_USER "ivan"
#define TEST_UID 1000
#define TEST_GID 100
#define OLD_PW "oldpass"
#define NEW_PW "newpass1"

static void out(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static void fail(const char *what)
{
	out("PASSWD_SMOKE_FAIL ");
	out(what);
	out("\n");
	_exit(1);
}

static int shadow_verifies(const char *user, const char *password)
{
	char hash[IR0_AUTH_HASH_MAX];

	if (ir0_shadow_hash(user, hash, sizeof(hash)) != 0)
		return -1;
	return ir0_password_verify(hash, password) ? 1 : 0;
}

/*
 * Run /bin/passwd as the unprivileged test user with @script on stdin, which
 * passwd(1) consumes in batch mode because the pipe is not a tty.
 */
static int run_passwd(const char *target, const char *script)
{
	int fds[2];
	pid_t pid;
	int status = 0;

	if (pipe(fds) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		char *argv[3];
		char *envp[2];

		(void)close(fds[1]);
		if (dup2(fds[0], 0) < 0)
			_exit(90);
		if (fds[0] > 2)
			(void)close(fds[0]);
		if (setgroups(0, NULL) != 0 && geteuid() == 0)
			_exit(91);
		if (setgid(TEST_GID) != 0)
			_exit(92);
		if (setuid(TEST_UID) != 0)
			_exit(93);

		argv[0] = "passwd";
		argv[1] = target ? (char *)target : NULL;
		argv[2] = NULL;
		envp[0] = "PATH=/bin:/sbin";
		envp[1] = NULL;
		execve("/bin/passwd", argv, envp);
		_exit(94);
	}

	(void)close(fds[0]);
	(void)write(fds[1], script, strlen(script));
	(void)close(fds[1]);

	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if ((status & 0x7f) != 0)
		return -1;
	return (status >> 8) & 0xff;
}

int main(void)
{
	char hash[IR0_AUTH_HASH_MAX];
	char root_before[IR0_AUTH_HASH_MAX];
	char root_after[IR0_AUTH_HASH_MAX];

	if (geteuid() != 0)
		fail("not_root");

	/* Baseline: a known SHA-512 hash written through the shared library. */
	if (ir0_password_hash(OLD_PW, hash, sizeof(hash)) != 0)
		fail("hash_old");
	if (strncmp(hash, "$6$", 3) != 0)
		fail("not_sha512");
	if (ir0_shadow_set_hash(TEST_USER, hash) != 0)
		fail("shadow_write");
	if (shadow_verifies(TEST_USER, OLD_PW) != 1)
		fail("baseline_verify");
	out("PASSWD_SETUP_OK\n");

	if (ir0_shadow_hash("root", root_before, sizeof(root_before)) != 0)
		fail("root_shadow_read");

	/* Positive: the user changes their own password after authenticating. */
	if (run_passwd(NULL, OLD_PW "\n" NEW_PW "\n" NEW_PW "\n") != 0)
		fail("passwd_exit");
	if (shadow_verifies(TEST_USER, NEW_PW) != 1)
		fail("new_password_rejected");
	if (shadow_verifies(TEST_USER, OLD_PW) != 0)
		fail("old_password_still_valid");
	if (ir0_shadow_hash(TEST_USER, hash, sizeof(hash)) != 0 ||
	    strncmp(hash, "$6$", 3) != 0)
		fail("new_hash_not_sha512");
	out("PASSWD_CHANGE_OK\n");

	/* Negative: a wrong current password must not rewrite the hash. */
	if (run_passwd(NULL, "wrongpass\n" OLD_PW "\n" OLD_PW "\n") == 0)
		fail("wrong_old_accepted");
	if (shadow_verifies(TEST_USER, NEW_PW) != 1)
		fail("hash_changed_after_denial");
	out("PASSWD_WRONG_OLD_OK\n");

	/* Negative: an unprivileged user cannot target another account. */
	if (run_passwd("root", "x\n" NEW_PW "\n" NEW_PW "\n") == 0)
		fail("other_user_accepted");
	if (ir0_shadow_hash("root", root_after, sizeof(root_after)) != 0)
		fail("root_shadow_reread");
	if (strcmp(root_before, root_after) != 0)
		fail("root_hash_modified");
	out("PASSWD_OTHER_DENIED_OK\n");

	out("PASSWD_ALL_OK\n");

	/* PID 1 must not exit: the harness kills QEMU once it sees the tag. */
	for (;;)
		(void)sleep(60);
	return 0;
}
