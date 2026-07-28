/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: matrix_capture.h
 * Description: BusyBox matrix worker capture — drain-to-EOF, streaming needles.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Storage cap for human/debug replay; matcher keeps observing past this. */
#ifndef MATRIX_CAPTURE_STORE_MAX
#define MATRIX_CAPTURE_STORE_MAX 1024u
#endif

/* Rolling window for cross-chunk needle search (needle_len - 1). */
#ifndef MATRIX_NEEDLE_WINDOW_MAX
#define MATRIX_NEEDLE_WINDOW_MAX 128u
#endif

/* Post-exit drain: wait slices while waiting for pipe EOF. */
#ifndef MATRIX_DRAIN_TIMEOUT_MS
#define MATRIX_DRAIN_TIMEOUT_MS 2000
#endif

/*
 * Sleep between nonblocking read attempts. Must use poll(NULL,0,ms) or
 * equivalent — never poll(fd, timeout>0) here (IR0 poll_waiter pool).
 */
#ifndef MATRIX_POLL_SLICE_MS
#define MATRIX_POLL_SLICE_MS 50
#endif

struct matrix_needle_matcher
{
	const char *needle;
	size_t needle_len;
	size_t matched; /* bytes of needle matched so far */
	int found;
	char window[MATRIX_NEEDLE_WINDOW_MAX];
	size_t window_len;
};

struct matrix_capture
{
	char *store; /* optional; may be NULL if only matching */
	size_t store_cap;
	size_t store_len;
	uint64_t bytes_seen;
	int truncated; /* store full but still reading */
	int saw_eof;
	int capture_timeout;
	struct matrix_needle_matcher matcher;
};

void matrix_needle_init(struct matrix_needle_matcher *m, const char *needle);
void matrix_needle_feed(struct matrix_needle_matcher *m, const void *data,
			size_t len);
int matrix_needle_found(const struct matrix_needle_matcher *m);

void matrix_capture_init(struct matrix_capture *c, char *store, size_t store_cap,
			 const char *needle);
void matrix_capture_feed(struct matrix_capture *c, const void *data, size_t len);

/*
 * Read from a non-blocking fd until EAGAIN (live worker) or EOF.
 * Returns: 1 = made progress / should continue, 0 = EOF, -1 = hard error.
 * When live_worker!=0, EAGAIN is success (return 1, no error).
 * When live_worker==0 (post-exit drain), EAGAIN means "poll again", return 1.
 */
int matrix_capture_read_nb(struct matrix_capture *c, int fd, int live_worker);

/*
 * After worker exit: poll+read until EOF or drain_timeout_ms.
 * Sets c->saw_eof or c->capture_timeout.
 * Returns 0 on EOF, -1 on timeout/error (capture_timeout set on timeout).
 */
int matrix_capture_drain_to_eof(struct matrix_capture *c, int fd,
			       int drain_timeout_ms);

/*
 * Simple FNV-1a 32-bit over stored bytes (not full stream if truncated).
 */
uint32_t matrix_capture_store_hash(const struct matrix_capture *c);

#ifdef __cplusplus
}
#endif
