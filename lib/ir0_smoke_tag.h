/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ir0_smoke_tag.h
 * Description: Userspace smoke/autokill tags (runit stages, runsv).
 * Equivalent to kernel klog_smoke(): bare token + newline greppable by
 * scripts/smoke_autokill.py and Makefile --done tags. Tags go to /dev/serial
 * so the human console (getty, login, banners) stays free of markers; stdout
 * is the fallback when the serial device cannot be opened.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <fcntl.h>
#include <unistd.h>

static inline void ir0_smoke_tag(const char *s)
{
	static int tag_fd = -2; /* -2 = not tried yet, -1 = fall back to stdout */
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;

	if (tag_fd == -2)
		tag_fd = open("/dev/serial", O_WRONLY);

	(void)write(tag_fd >= 0 ? tag_fd : 1, s, (size_t)(p - s));
}
