/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: busybox_matrix_smoke.c
 * Description: PID1 driver that classifies BusyBox applets by real behaviour.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define WORKDIR "/tmp/bbm"
#define DATA_FILE WORKDIR "/a.txt"
#define DATA_TEXT "alpha\nbeta\ngamma\n"
#define OUT_MAX 1024
/* Per-applet wall clock: df/mount have hung forever on MINIX mount walks. */
#define CASE_TIMEOUT_MS 4000

struct bb_case
{
	const char *applet;
	/* argv after the applet name, NULL terminated (max 4 arguments). */
	const char *argv[5];
	const char *needle;
	int want_ec;
	/* When set, the child reads this file on stdin instead of /dev/null. */
	const char *stdin_path;
};

/*
 * Every case runs the applet for real inside IR0. Nothing here asserts that an
 * applet "works" because it compiled: the exit code plus the expected output is
 * what promotes an applet to "supported".
 */
static const struct bb_case cases[] = {
	{ "echo", { "hi", NULL }, "hi", 0, NULL },
	{ "cat", { DATA_FILE, NULL }, "alpha", 0, NULL },
	{ "ls", { WORKDIR, NULL }, "a.txt", 0, NULL },
	{ "pwd", { NULL }, "/", 0, NULL },
	{ "mkdir", { WORKDIR "/d1", NULL }, NULL, 0, NULL },
	{ "rmdir", { WORKDIR "/d1", NULL }, NULL, 0, NULL },
	{ "touch", { WORKDIR "/t1", NULL }, NULL, 0, NULL },
	{ "cp", { DATA_FILE, WORKDIR "/b.txt", NULL }, NULL, 0, NULL },
	{ "mv", { WORKDIR "/b.txt", WORKDIR "/c.txt", NULL }, NULL, 0, NULL },
	{ "rm", { WORKDIR "/c.txt", NULL }, NULL, 0, NULL },
	{ "ln", { DATA_FILE, WORKDIR "/l1", NULL }, NULL, 0, NULL },
	{ "stat", { DATA_FILE, NULL }, NULL, 0, NULL },
	{ "chmod", { "644", DATA_FILE, NULL }, NULL, 0, NULL },
	{ "basename", { "/a/b", NULL }, "b", 0, NULL },
	{ "dirname", { "/a/b", NULL }, "/a", 0, NULL },
	{ "true", { NULL }, NULL, 0, NULL },
	{ "false", { NULL }, NULL, 1, NULL },
	{ "test", { "-f", DATA_FILE, NULL }, NULL, 0, NULL },
	{ "uname", { NULL }, "IR0", 0, NULL },
	{ "sleep", { "0", NULL }, NULL, 0, NULL },
	{ "printf", { "x\\n", NULL }, "x", 0, NULL },
	{ "env", { NULL }, "PATH", 0, NULL },
	{ "which", { "ls", NULL }, "/bin/ls", 0, NULL },
	{ "id", { NULL }, "uid=0", 0, NULL },
	{ "sync", { NULL }, NULL, 0, NULL },
	{ "clear", { NULL }, NULL, 0, NULL },
};

static void put(const char *s)
{
	if (s)
		(void)write(1, s, strlen(s));
}

static void put_int(int v)
{
	char buf[16];
	int i = (int)sizeof(buf);
	int neg = v < 0;
	unsigned int u = neg ? (unsigned int)-v : (unsigned int)v;

	buf[--i] = '\0';
	do
	{
		buf[--i] = (char)('0' + (u % 10));
		u /= 10;
	} while (u && i > 1);
	if (neg && i > 0)
		buf[--i] = '-';
	put(&buf[i]);
}

static int seed_workdir(void)
{
	int fd;

	(void)mkdir(WORKDIR, 0755);
	fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, DATA_TEXT, strlen(DATA_TEXT)) < 0)
	{
		(void)close(fd);
		return -1;
	}
	(void)close(fd);

	/* gunzip -t needs an input file even when the applet is unavailable. */
	fd = open(WORKDIR "/g.gz", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
		(void)close(fd);
	return 0;
}

static int run_case(const struct bb_case *c, char *out, size_t out_sz,
		    int *exit_code)
{
	int fds[2];
	pid_t pid;
	pid_t dog;
	int status = 0;
	size_t total = 0;
	int timed_out = 0;

	if (pipe(fds) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
	{
		(void)close(fds[0]);
		(void)close(fds[1]);
		return -1;
	}

	if (pid == 0)
	{
		char *argv[8];
		char *envp[3];
		int in_fd;
		int i = 0;

		(void)close(fds[0]);
		(void)dup2(fds[1], 1);
		(void)dup2(fds[1], 2);
		if (fds[1] > 2)
			(void)close(fds[1]);

		in_fd = open(c->stdin_path ? c->stdin_path : "/dev/null",
			     O_RDONLY);
		if (in_fd >= 0)
		{
			(void)dup2(in_fd, 0);
			if (in_fd > 2)
				(void)close(in_fd);
		}

		argv[i++] = "busybox";
		argv[i++] = (char *)c->applet;
		for (int a = 0; c->argv[a] && i < 7; a++)
			argv[i++] = (char *)c->argv[a];
		argv[i] = NULL;
		envp[0] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
		envp[1] = "HOME=/root";
		envp[2] = NULL;

		execve("/bin/busybox", argv, envp);
		_exit(127);
	}

	(void)close(fds[1]);

	/*
	 * Watchdog sibling: O_NONBLOCK on pipes is unreliable on IR0, so a
	 * blocked read() would hang the matrix forever (seen with df/mount).
	 * Killing the applet closes the pipe and unblocks the parent.
	 */
	dog = fork();
	if (dog < 0)
	{
		/*
		 * Process table full after many cases: without a watchdog a hung
		 * applet would stall the matrix forever. Abort this case cleanly.
		 */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		out[0] = '\0';
		(void)close(fds[0]);
		*exit_code = 125;
		return 0;
	}
	if (dog == 0)
	{
		/* Prefer sleep(2): usleep has hung the matrix watchdog on IR0. */
		(void)sleep((unsigned)(CASE_TIMEOUT_MS / 1000));
		if (kill(pid, 0) == 0)
			(void)kill(pid, SIGKILL);
		_exit(1);
	}

	while (total + 1 < out_sz)
	{
		ssize_t n = read(fds[0], out + total, out_sz - 1 - total);

		if (n <= 0)
			break;
		total += (size_t)n;
	}
	out[total] = '\0';
	(void)close(fds[0]);

	if (waitpid(pid, &status, 0) != pid)
	{
		if (dog > 0)
		{
			(void)kill(dog, SIGKILL);
			(void)waitpid(dog, NULL, 0);
		}
		return -1;
	}
	if (dog > 0)
	{
		int dog_st = 0;
		pid_t dw = waitpid(dog, &dog_st, WNOHANG);

		if (dw == 0)
		{
			(void)kill(dog, SIGKILL);
			(void)waitpid(dog, &dog_st, 0);
		}
		/* Watchdog exits 1 only after it SIGKILLed the applet. */
		if (dw == dog && ((dog_st >> 8) & 0xff) == 1)
			timed_out = 1;
		else if ((status & 0x7f) == SIGKILL)
			timed_out = 1;
	}

	if (timed_out)
	{
		*exit_code = 124;
		return 0;
	}
	if ((status & 0x7f) != 0)
	{
		*exit_code = 128 + (status & 0x7f);
		return 0;
	}
	*exit_code = (status >> 8) & 0xff;
	return 0;
}

/*
 * supported   — expected exit status and expected output
 * partial     — applet ran but the contract is not met yet
 * unavailable — applet missing from the binary, or the kernel path is absent
 */
static const char *classify(const struct bb_case *c, const char *out, int ec,
			    const char **reason)
{
	*reason = "-";

	if (ec == 127 || strstr(out, "applet not found"))
	{
		*reason = "missing";
		return "unavailable";
	}
	if (ec == 124)
	{
		*reason = "timeout";
		return "unavailable";
	}
	if (ec == 125)
	{
		*reason = "nofork";
		return "partial";
	}
	if (strstr(out, "not implemented") || strstr(out, "ENOSYS"))
	{
		*reason = "enosys";
		return "unavailable";
	}
	if (ec != c->want_ec)
	{
		*reason = "exit";
		return "partial";
	}
	if (c->needle && !strstr(out, c->needle))
	{
		*reason = "output";
		return "partial";
	}
	return "supported";
}

int main(void)
{
	char out[OUT_MAX];
	size_t i;
	int supported = 0;
	int partial = 0;
	int unavailable = 0;
	const size_t ncases = sizeof(cases) / sizeof(cases[0]);

	put("BBMATRIX_START\n");
	if (seed_workdir() != 0)
	{
		put("BBMATRIX_FAIL workdir\n");
		for (;;)
			(void)sleep(60);
	}

	for (i = 0; i < ncases; i++)
	{
		const char *status;
		const char *reason;
		int ec = -1;

		out[0] = '\0';
		if (run_case(&cases[i], out, sizeof(out), &ec) != 0)
		{
			status = "partial";
			reason = "harness";
		}
		else
		{
			status = classify(&cases[i], out, ec, &reason);
		}

		if (strcmp(status, "supported") == 0)
			supported++;
		else if (strcmp(status, "partial") == 0)
			partial++;
		else
			unavailable++;

		put("BBMATRIX applet=");
		put(cases[i].applet);
		put(" status=");
		put(status);
		put(" ec=");
		put_int(ec);
		put(" reason=");
		put(reason);
		put("\n");
	}

	put("BBMATRIX_TOTAL cases=");
	put_int((int)ncases);
	put(" supported=");
	put_int(supported);
	put(" partial=");
	put_int(partial);
	put(" unavailable=");
	put_int(unavailable);
	put("\n");
	put("BBMATRIX_OK\n");

	/* PID 1 must not exit: the harness kills QEMU once it sees the tag. */
	for (;;)
		(void)sleep(60);
	return 0;
}
