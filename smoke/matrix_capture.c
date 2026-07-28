/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: matrix_capture.c
 * Description: BusyBox matrix capture — drain-to-EOF and streaming needles.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include "matrix_capture.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void matrix_needle_init(struct matrix_needle_matcher *m, const char *needle)
{
	size_t n;

	if (!m)
		return;
	memset(m, 0, sizeof(*m));
	m->needle = needle;
	if (!needle)
	{
		m->needle_len = 0;
		return;
	}
	n = strlen(needle);
	if (n > MATRIX_NEEDLE_WINDOW_MAX)
		n = MATRIX_NEEDLE_WINDOW_MAX;
	m->needle_len = n;
}

void matrix_needle_feed(struct matrix_needle_matcher *m, const void *data,
			size_t len)
{
	const unsigned char *p;
	size_t i;

	if (!m || m->found || !m->needle || m->needle_len == 0 || !data ||
	    len == 0)
		return;

	p = (const unsigned char *)data;
	for (i = 0; i < len; i++)
	{
		unsigned char ch = p[i];

		if (ch == (unsigned char)m->needle[m->matched])
		{
			m->matched++;
			if (m->matched == m->needle_len)
			{
				m->found = 1;
				return;
			}
			continue;
		}
		/*
		 * Mismatch: restart from scratch, but ch might start the needle
		 * (naive; good enough for short BusyBox needles).
		 */
		if (m->matched > 0)
		{
			m->matched = 0;
			if (ch == (unsigned char)m->needle[0])
				m->matched = 1;
		}
	}
	(void)m->window;
	(void)m->window_len;
}

int matrix_needle_found(const struct matrix_needle_matcher *m)
{
	return m && m->found;
}

void matrix_capture_init(struct matrix_capture *c, char *store, size_t store_cap,
			 const char *needle)
{
	if (!c)
		return;
	memset(c, 0, sizeof(*c));
	c->store = store;
	c->store_cap = store_cap;
	if (store && store_cap > 0)
		store[0] = '\0';
	matrix_needle_init(&c->matcher, needle);
}

void matrix_capture_feed(struct matrix_capture *c, const void *data, size_t len)
{
	size_t room;
	size_t copy;

	if (!c || !data || len == 0)
		return;

	matrix_needle_feed(&c->matcher, data, len);
	c->bytes_seen += (uint64_t)len;

	if (!c->store || c->store_cap < 2)
		return;

	room = c->store_cap - 1u - c->store_len;
	if (room == 0)
	{
		c->truncated = 1;
		return;
	}
	copy = len < room ? len : room;
	memcpy(c->store + c->store_len, data, copy);
	c->store_len += copy;
	c->store[c->store_len] = '\0';
	if (copy < len)
		c->truncated = 1;
}

/*
 * IR0 sys_poll allocates a global poll_waiter when timeout_ms != 0.
 * MAX_POLL_WAITERS is small (16). Blocking poll from the matrix parent
 * under load returned -EAGAIN, and older drain code treated that as a
 * hard stop → false no-eof / lost needles.
 *
 * Contract: only poll with timeout 0 (ready check, no waiter) or nfds=0
 * (pure sleep). Never block in poll(fd, timeout>0) from this library.
 */
static void matrix_brief_wait_ms(int ms)
{
	struct timespec ts;
	struct timespec rem;

	/*
	 * Prefer nanosleep over poll(NULL,0,ms). A non-blocking nfds=0 poll
	 * would busy-loop the capturer and starve the pipe writer / exit path.
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

static unsigned long long matrix_now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		(void)clock_gettime(CLOCK_REALTIME, &ts);
	return (unsigned long long)ts.tv_sec * 1000ull +
	       (unsigned long long)ts.tv_nsec / 1000000ull;
}

int matrix_capture_read_nb(struct matrix_capture *c, int fd, int live_worker)
{
	char chunk[256];
	ssize_t n;
	int progress = 0;

	if (!c || fd < 0)
		return -1;

	for (;;)
	{
		n = read(fd, chunk, sizeof(chunk));
		if (n > 0)
		{
			matrix_capture_feed(c, chunk, (size_t)n);
			progress = 1;
			continue;
		}
		if (n == 0)
		{
			c->saw_eof = 1;
			return 0;
		}
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			(void)live_worker;
			return 1;
		}
		return -1;
	}
}

int matrix_capture_drain_to_eof(struct matrix_capture *c, int fd,
			       int drain_timeout_ms)
{
	unsigned long long deadline_ms;
	int slice;

	if (!c || fd < 0)
		return -1;

	if (drain_timeout_ms <= 0)
		drain_timeout_ms = MATRIX_DRAIN_TIMEOUT_MS;
	deadline_ms = matrix_now_ms() + (unsigned long long)drain_timeout_ms;
	slice = MATRIX_POLL_SLICE_MS;
	if (slice < 1)
		slice = 1;

	while (!c->saw_eof)
	{
		struct pollfd pfd;
		int pr;
		int rr;

		if (matrix_now_ms() >= deadline_ms)
		{
			c->capture_timeout = 1;
			return -1;
		}

		/*
		 * timeout=0: readiness only — must not allocate IR0 poll_waiter.
		 */
		pfd.fd = fd;
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		pr = poll(&pfd, 1, 0);
		if (pr < 0)
		{
			if (errno == EINTR || errno == EAGAIN ||
			    errno == EWOULDBLOCK)
			{
				matrix_brief_wait_ms(slice);
				continue;
			}
			c->capture_timeout = 1;
			return -1;
		}

		rr = matrix_capture_read_nb(c, fd, 0);
		if (rr == 0)
			return 0;
		if (rr < 0)
		{
			c->capture_timeout = 1;
			return -1;
		}
		/*
		 * EAGAIN after exit: writer may still be flushing into the
		 * pipe; keep waiting until EOF or timeout. Never treat EAGAIN
		 * as end-of-capture (that was the matrix reason=output flake).
		 */
		matrix_brief_wait_ms(slice);
	}
	return 0;
}

uint32_t matrix_capture_store_hash(const struct matrix_capture *c)
{
	uint32_t h = 2166136261u;
	size_t i;

	if (!c || !c->store)
		return 0;
	for (i = 0; i < c->store_len; i++)
	{
		h ^= (uint32_t)(unsigned char)c->store[i];
		h *= 16777619u;
	}
	return h;
}
