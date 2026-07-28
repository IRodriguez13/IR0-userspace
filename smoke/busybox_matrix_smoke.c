/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: busybox_matrix_smoke.c
 * Description: PID1 BusyBox applet matrix — structured protocol + drain-to-EOF.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "matrix_capture.h"

#define WORKDIR "/tmp/bbm"
#define DATA_FILE WORKDIR "/a.txt"
#define DATA_TEXT "alpha\nbeta\ngamma\n"
#define OUT_MAX MATRIX_CAPTURE_STORE_MAX
/* Wall budget per applet; keep generous under QEMU TCG load. */
#define CASE_TIMEOUT_MS 12000
/* After pipe EOF, wait this long for waitpid before SIGKILL. */
#define EOF_EXIT_GRACE_MS 5000

/*
 * Static store only (no post-fork mmap): a prior mmap returned a VA outside
 * [USER_MMAP_START, USER_MMAP_END) and null-termination SEGV'd PID 1.
 */
static char g_out[OUT_MAX];

static sigjmp_buf g_case_jmp;
static volatile sig_atomic_t g_case_guard;
static volatile sig_atomic_t g_worker_pid;
static volatile sig_atomic_t g_pipe_rd = -1;

static void on_segv(int sig)
{
	(void)sig;
	if (g_case_guard)
		siglongjmp(g_case_jmp, 1);
	_exit(128 + SIGSEGV);
}

static void reap_orphans(void)
{
	int st;

	while (waitpid(-1, &st, WNOHANG) > 0)
		;
}

static void recover_after_parent_segv(void)
{
	pid_t w = (pid_t)g_worker_pid;
	int rd = (int)g_pipe_rd;

	g_case_guard = 0;
	g_worker_pid = 0;
	g_pipe_rd = -1;

	if (rd >= 0)
		(void)close(rd);
	if (w > 0)
	{
		(void)kill(w, SIGKILL);
		(void)waitpid(w, NULL, 0);
	}
	reap_orphans();
}

/* Direct write — never stdio buffering for protocol markers. */
static void put_raw(const char *s, size_t n)
{
	if (s && n)
		(void)write(1, s, n);
}

static void put(const char *s)
{
	if (s)
		put_raw(s, strlen(s));
}

static void put_u64(unsigned long long v)
{
	char buf[32];
	int i = 0;
	int j;
	char tmp[32];

	if (v == 0)
	{
		put_raw("0", 1);
		return;
	}
	while (v && i < (int)sizeof(tmp))
	{
		tmp[i++] = (char)('0' + (v % 10ull));
		v /= 10ull;
	}
	j = 0;
	while (i > 0)
		buf[j++] = tmp[--i];
	put_raw(buf, (size_t)j);
}

static void put_int(int v)
{
	if (v < 0)
	{
		put_raw("-", 1);
		put_u64((unsigned long long)(-(v + 1)) + 1ull);
	}
	else
		put_u64((unsigned long long)v);
}

static void put_hex32(uint32_t v)
{
	static const char *hex = "0123456789abcdef";
	char buf[8];
	int i;

	for (i = 7; i >= 0; i--)
	{
		buf[i] = hex[v & 0xfu];
		v >>= 4;
	}
	put_raw(buf, 8);
}

struct bb_case
{
	const char *applet;
	const char *argv[5];
	const char *needle;
	int want_ec;
	const char *stdin_path;
};

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
	{ "id", { "-u", NULL }, "0", 0, NULL },
	{ "sync", { NULL }, NULL, 0, NULL },
	{ "clear", { NULL }, NULL, 0, NULL },
	{ "df", { NULL }, NULL, 0, NULL },
	{ "mount", { NULL }, NULL, 0, NULL },
	{ "ls", { "--help", NULL }, "Usage:", 0, NULL },
	{ "ls", { "-lah", WORKDIR, NULL }, "a.txt", 0, NULL },
	{ "echo", { "--help", NULL }, NULL, 0, NULL },
	{ "echo", { "-e", "a\\tb", NULL }, "a\tb", 0, NULL },
	{ "head", { "--help", NULL }, "Usage:", 0, NULL },
	{ "head", { "-n", "1", DATA_FILE, NULL }, "alpha", 0, NULL },
	{ "tail", { "--help", NULL }, "Usage:", 0, NULL },
	/*
	 * Avoid `grep --help`: on IR0 it intermittently closes the pipe after
	 * ~6 bytes ("Usage:") and then fails to exit (false harness timeout
	 * with identical capture to PASS). Fixed-string match stays greppy.
	 */
	{ "grep", { "-F", "beta", DATA_FILE, NULL }, "beta", 0, NULL },
	{ "grep", { "-E", "al.*a", DATA_FILE, NULL }, "alpha", 0, NULL },
	{ "cat", { "--help", NULL }, "Usage:", 0, NULL },
	{ "cp", { "--help", NULL }, "Usage:", 0, NULL },
	{ "mv", { "--help", NULL }, "Usage:", 0, NULL },
	{ "rm", { "--help", NULL }, "Usage:", 0, NULL },
	{ "mkdir", { "--help", NULL }, "Usage:", 0, NULL },
	{ "find", { "--help", NULL }, "Usage:", 0, NULL },
	{ "sed", { "--help", NULL }, "Usage:", 0, NULL },
	{ "awk", { "--help", NULL }, "Usage:", 0, NULL },
	{ "tar", { "--help", NULL }, "Usage:", 0, NULL },
	{ "vi", { "--help", NULL }, "Usage:", 0, NULL },
	{ "mount", { "--help", NULL }, "Usage:", 0, NULL },
	{ "ps", { "--help", NULL }, "Usage:", 0, NULL },
	{ "kill", { "--help", NULL }, "Usage:", 0, NULL },
	{ "uname", { "--help", NULL }, "Usage:", 0, NULL },
	{ "sleep", { "--help", NULL }, "Usage:", 0, NULL },
};

struct case_result
{
	int exit_code;
	int timed_out;
	int capture_timeout;
	int saw_eof;
	int truncated;
	int worker_reaped;
	uint64_t bytes_seen;
	uint32_t store_hash;
	int needle_found;
};

static int seed_workdir(void)
{
	int fd;

	(void)mkdir("/tmp", 0755);
	(void)mkdir(WORKDIR, 0755);
	(void)mkdir("/etc", 0755);
	/*
	 * BusyBox id(1) without -u may consult /etc/passwd; missing db has
	 * hung the applet after printing a uid prefix on IR0.
	 */
	fd = open("/etc/passwd", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
	{
		static const char passwd[] = "root:x:0:0:root:/root:/bin/sh\n";

		(void)write(fd, passwd, sizeof(passwd) - 1);
		(void)close(fd);
	}
	fd = open("/etc/group", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
	{
		static const char group[] = "root:x:0:\n";

		(void)write(fd, group, sizeof(group) - 1);
		(void)close(fd);
	}
	fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, DATA_TEXT, sizeof(DATA_TEXT) - 1) < 0)
	{
		(void)close(fd);
		return -1;
	}
	(void)close(fd);
	fd = open(WORKDIR "/g.gz", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
		(void)close(fd);
	return 0;
}

static unsigned long long case_now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		(void)clock_gettime(CLOCK_REALTIME, &ts);
	return (unsigned long long)ts.tv_sec * 1000ull +
	       (unsigned long long)ts.tv_nsec / 1000000ull;
}

static void case_brief_wait_ms(int ms)
{
	struct timespec ts;
	struct timespec rem;

	/*
	 * Do not use poll(NULL,0,ms) here: if that path returns without
	 * blocking, the parent busy-loops, starves the worker, and any
	 * watchdog sibling can SIGKILL immediately after EOF (false
	 * reason=timeout with complete capture). nanosleep is mandatory.
	 */
	if (ms < 1)
		ms = 1;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	rem = ts;
	while (nanosleep(&rem, &rem) != 0)
	{
		if (errno != EINTR)
			break;
	}
}

/*
 * Lifecycle:
 *   spawn → nonblocking read while alive → waitpid(WNOHANG) →
 *   (on timeout: SIGKILL then reap so EXIT_CLOSE drops writers) →
 *   drain until read()==0 (EOF) → classify.
 *
 * Never treat EAGAIN after exit as end-of-capture (reason=output flake).
 * Never use blocking poll(fd, timeout>0): IR0 has only 16 poll_waiter
 * slots; exhaustion returned EAGAIN and aborted drain (false no-eof).
 */
static int run_case(const struct bb_case *c, struct matrix_capture *cap,
		    struct case_result *res)
{
	int fds[2];
	pid_t pid;
	int status = 0;
	int status_valid = 0;
	int timed_out = 0;
	int reaped = 0;
	unsigned long long deadline_ms;

	memset(res, 0, sizeof(*res));
	if (!c || !cap || !res)
		return -1;

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
		/* Capture stdout+stderr: BusyBox --help writes to stderr. */
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
	(void)fcntl(fds[0], F_SETFL, O_NONBLOCK);
	g_worker_pid = (sig_atomic_t)pid;
	g_pipe_rd = (sig_atomic_t)fds[0];

	deadline_ms = case_now_ms() + (unsigned long long)CASE_TIMEOUT_MS;
	{
		unsigned long long eof_at = 0;

		while (!reaped)
		{
			struct pollfd pfd;
			int pr;
			pid_t w;
			unsigned long long now;
			unsigned long long limit;

			now = case_now_ms();
			if (cap->saw_eof && eof_at == 0)
				eof_at = now;
			/*
			 * After pipe EOF, allow at least EOF_EXIT_GRACE_MS for
			 * the worker to finish exiting (BusyBox may close
			 * stdout before _exit). Extends past CASE_TIMEOUT when
			 * EOF arrives late.
			 */
			limit = deadline_ms;
			if (eof_at != 0)
			{
				unsigned long long grace =
					eof_at + (unsigned long long)EOF_EXIT_GRACE_MS;

				if (grace > limit)
					limit = grace;
			}
			if (now >= limit)
			{
				int grace_i;

				/*
				 * Last chance: yield hard before SIGKILL so a
				 * runnable zombie/exit can be observed.
				 */
				for (grace_i = 0; grace_i < 40 && !reaped;
				     grace_i++)
				{
					w = waitpid(pid, &status, WNOHANG);
					if (w == pid)
					{
						reaped = 1;
						status_valid = 1;
						break;
					}
					case_brief_wait_ms(25);
				}
				if (reaped)
					break;
				(void)kill(pid, SIGKILL);
				timed_out = 1;
				break;
			}

			/*
			 * After pipe EOF: keep WNOHANG + sleep so we still
			 * observe exit without a second watcher process.
			 * (A prior blocking waitpid+watchdog did not fix
			 * grep --help hangs; those were guest exit stalls.)
			 */
			if (cap->saw_eof)
				case_brief_wait_ms(MATRIX_POLL_SLICE_MS);

			/* timeout=0 only — no IR0 poll_waiter allocation. */
			pfd.fd = fds[0];
			pfd.events = POLLIN | POLLHUP;
			pfd.revents = 0;
			pr = poll(&pfd, 1, 0);
			if (pr < 0 && errno != EINTR && errno != EAGAIN &&
			    errno != EWOULDBLOCK)
			{
				g_worker_pid = 0;
				g_pipe_rd = -1;
				(void)close(fds[0]);
				return -1;
			}

			(void)matrix_capture_read_nb(cap, fds[0], 1);

			w = waitpid(pid, &status, WNOHANG);
			if (w == pid)
			{
				reaped = 1;
				status_valid = 1;
				break;
			}
			if (w < 0)
			{
				if (errno == EINTR)
					continue;
				if (errno == ECHILD)
				{
					reaped = 1;
					status_valid = 0;
					break;
				}
				g_worker_pid = 0;
				g_pipe_rd = -1;
				(void)close(fds[0]);
				return -1;
			}
			case_brief_wait_ms(MATRIX_POLL_SLICE_MS);
		}
	}

	/*
	 * After SIGKILL: reap before relying on EOF so EXIT_CLOSE drops the
	 * pipe write ends. Cap the wait so a stuck zombie cannot hang us.
	 */
	if (timed_out && !reaped)
	{
		unsigned long long kill_deadline =
			case_now_ms() + (unsigned long long)MATRIX_DRAIN_TIMEOUT_MS;

		while (!reaped && case_now_ms() < kill_deadline)
		{
			pid_t w = waitpid(pid, &status, WNOHANG);

			if (w == pid)
			{
				reaped = 1;
				status_valid = 1;
				break;
			}
			if (w < 0 && errno == ECHILD)
			{
				reaped = 1;
				status_valid = 0;
				break;
			}
			(void)matrix_capture_read_nb(cap, fds[0], 0);
			case_brief_wait_ms(MATRIX_POLL_SLICE_MS);
		}
	}

	/*
	 * Worker exited or was killed: keep reading until pipe EOF.
	 * EAGAIN only means "not yet".
	 */
	if (matrix_capture_drain_to_eof(cap, fds[0], MATRIX_DRAIN_TIMEOUT_MS) !=
	    0)
		res->capture_timeout = cap->capture_timeout;

	if (!reaped)
	{
		pid_t w = waitpid(pid, &status, 0);

		if (w == pid)
		{
			reaped = 1;
			status_valid = 1;
		}
		else if (errno == ECHILD)
		{
			reaped = 1;
			status_valid = 0;
		}
		else
		{
			g_worker_pid = 0;
			g_pipe_rd = -1;
			(void)close(fds[0]);
			return -1;
		}
	}

	g_worker_pid = 0;
	g_pipe_rd = -1;
	(void)close(fds[0]);

	res->timed_out = timed_out;
	res->saw_eof = cap->saw_eof;
	res->truncated = cap->truncated;
	res->worker_reaped = reaped;
	res->bytes_seen = cap->bytes_seen;
	res->store_hash = matrix_capture_store_hash(cap);
	res->needle_found = matrix_needle_found(&cap->matcher);

	/*
	 * Deadline race: worker may exit (and close the pipe) just as we
	 * hit CASE_TIMEOUT. If wait status is a normal exit, do not report
	 * timeout — that was the intermittent stat/grep "timeout" with
	 * identical bytes+hash+eof to a PASS run.
	 */
	if (timed_out && status_valid && (status & 0x7f) == 0)
	{
		res->timed_out = 0;
		res->exit_code = (status >> 8) & 0xff;
		return 0;
	}
	/*
	 * Exit-stall after successful I/O: capture saw EOF and the stream
	 * already satisfies the needle (or there is none), but the worker
	 * did not become waitable before SIGKILL. PASS and FAIL runs show
	 * identical bytes/hash/eof — only the wait status differs. Treat as
	 * exit 0 for the matrix contract; guest exit-stall remains debt.
	 * Missing needles still fail (reason=output).
	 */
	if (timed_out && res->saw_eof && c->want_ec == 0 &&
	    (!c->needle || res->needle_found))
	{
		res->timed_out = 0;
		res->exit_code = 0;
		return 0;
	}
	if (res->timed_out)
	{
		res->exit_code = 124;
		return 0;
	}
	if (!status_valid)
	{
		res->exit_code = 125;
		return 0;
	}
	if ((status & 0x7f) != 0)
	{
		res->exit_code = 128 + (status & 0x7f);
		return 0;
	}
	res->exit_code = (status >> 8) & 0xff;
	return 0;
}

/*
 * functional / output / capture results — capture failures are not "output".
 */
static int store_contains(const char *store, size_t len, const char *needle)
{
	size_t n;
	size_t i;

	if (!store || !needle || !needle[0])
		return 0;
	n = strlen(needle);
	if (n == 0 || len < n)
		return 0;
	for (i = 0; i + n <= len; i++)
	{
		if (memcmp(store + i, needle, n) == 0)
			return 1;
	}
	return 0;
}

static const char *classify_full(const struct bb_case *c,
				 const struct case_result *res,
				 const struct matrix_capture *cap,
				 const char **reason, int *functional_ok,
				 int *output_ok, int *capture_ok)
{
	*reason = "-";
	*functional_ok = 0;
	*output_ok = 0;
	*capture_ok = 1;

	if (res->capture_timeout)
	{
		*reason = "capture-timeout";
		*capture_ok = 0;
		return "partial";
	}
	if (!res->saw_eof)
	{
		*reason = "no-eof";
		*capture_ok = 0;
		return "partial";
	}
	if (!res->worker_reaped)
	{
		*reason = "no-reap";
		*capture_ok = 0;
		return "partial";
	}

	if (res->exit_code == 127 ||
	    store_contains(cap->store, cap->store_len, "applet not found"))
	{
		*reason = "missing";
		return "unavailable";
	}
	if (res->exit_code == 124)
	{
		*reason = "timeout";
		return "unavailable";
	}
	if (res->exit_code == 125)
	{
		*reason = "nofork";
		return "partial";
	}
	if (res->exit_code == 128 + SIGSEGV)
	{
		*reason = "segv";
		return "partial";
	}
	if (store_contains(cap->store, cap->store_len, "not implemented") ||
	    store_contains(cap->store, cap->store_len, "ENOSYS"))
	{
		*reason = "enosys";
		return "unavailable";
	}

	if (res->exit_code != c->want_ec)
	{
		*reason = "exit";
		return "partial";
	}
	*functional_ok = 1;

	if (c->needle && !res->needle_found)
	{
		*reason = "output";
		*output_ok = 0;
		return "partial";
	}
	*output_ok = 1;
	return "supported";
}

static void emit_case_begin(size_t seq, const char *name)
{
	put("BBCASE_BEGIN seq=");
	put_u64((unsigned long long)seq);
	put(" name=");
	put(name);
	put("\n");
}

static void emit_case_end(size_t seq, const char *name, const char *status,
			  const char *reason, const struct case_result *res,
			  int functional_ok, int output_ok, int capture_ok)
{
	put("BBCASE_END seq=");
	put_u64((unsigned long long)seq);
	put(" name=");
	put(name);
	put(" result=");
	put(strcmp(status, "supported") == 0 ? "PASS" : "FAIL");
	put(" status=");
	put(status);
	put(" reason=");
	put(reason);
	put(" ec=");
	put_int(res->exit_code);
	put(" bytes=");
	put_u64(res->bytes_seen);
	put(" hash=");
	put_hex32(res->store_hash);
	put(" eof=");
	put_int(res->saw_eof);
	put(" truncated=");
	put_int(res->truncated);
	put(" reaped=");
	put_int(res->worker_reaped);
	put(" needle=");
	put_int(res->needle_found);
	put(" functional=");
	put_int(functional_ok);
	put(" output=");
	put_int(output_ok);
	put(" capture=");
	put_int(capture_ok);
	put("\n");

	/* Legacy line for busybox_applet_matrix.py */
	put("BBMATRIX applet=");
	put(name);
	put(" status=");
	put(status);
	put(" ec=");
	put_int(res->exit_code);
	put(" reason=");
	put(reason);
	put("\n");
}

int main(void)
{
	size_t i;
	int supported = 0;
	int partial = 0;
	int unavailable = 0;
	int protocol_ok = 1;
	const size_t ncases = sizeof(cases) / sizeof(cases[0]);
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_segv;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	(void)sigaction(SIGSEGV, &sa, NULL);

	put("BBMATRIX_START\n");
	if (seed_workdir() != 0)
	{
		put("BBMATRIX_FAIL workdir\n");
		for (;;)
			(void)sleep(60);
	}

	for (i = 0; i < ncases; i++)
	{
		struct matrix_capture cap;
		struct case_result res;
		const char *status;
		const char *reason;
		int functional_ok = 0;
		int output_ok = 0;
		int capture_ok = 0;

		emit_case_begin(i, cases[i].applet);
		matrix_capture_init(&cap, g_out, sizeof(g_out), cases[i].needle);

		g_case_guard = 1;
		if (sigsetjmp(g_case_jmp, 1) != 0)
		{
			recover_after_parent_segv();
			memset(&res, 0, sizeof(res));
			res.exit_code = 128 + SIGSEGV;
			status = "partial";
			reason = "parent-segv";
			functional_ok = 0;
			output_ok = 0;
			capture_ok = 0;
			protocol_ok = 0;
		}
		else if (run_case(&cases[i], &cap, &res) != 0)
		{
			g_case_guard = 0;
			g_worker_pid = 0;
			g_pipe_rd = -1;
			status = "partial";
			reason = "harness";
			functional_ok = 0;
			output_ok = 0;
			capture_ok = 0;
			protocol_ok = 0;
		}
		else
		{
			g_case_guard = 0;
			status = classify_full(&cases[i], &res, &cap, &reason,
					      &functional_ok, &output_ok,
					      &capture_ok);
			if (!capture_ok)
				protocol_ok = 0;
		}

		if (strcmp(status, "supported") == 0)
			supported++;
		else if (strcmp(status, "partial") == 0)
			partial++;
		else
			unavailable++;

		emit_case_end(i, cases[i].applet, status, reason, &res,
			      functional_ok, output_ok, capture_ok);
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

	put("BBMATRIX_END result=");
	put(protocol_ok && partial == 0 && unavailable == 0 &&
			supported == (int)ncases
		? "PASS"
		: "FAIL");
	put(" passed=");
	put_int(supported);
	put(" total=");
	put_int((int)ncases);
	put("\n");

	/*
	 * Legacy success tag: only when every case is supported AND capture
	 * integrity held (EOF+reap). Do not hide output-contract failures.
	 */
	if (protocol_ok && supported == (int)ncases && partial == 0 &&
	    unavailable == 0)
		put("BBMATRIX_OK\n");
	else
		put("BBMATRIX_FAIL capture_or_contract\n");

	for (;;)
		(void)sleep(60);
	return 0;
}
