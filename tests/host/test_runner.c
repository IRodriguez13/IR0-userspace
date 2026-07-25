/**
 * IR0 Userspace — Unix product tree
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_runner.c
 * Description: Host test runner for the userspace product libraries.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

int _ir0_test_failed = 0;
int _ir0_test_count = 0;
int _ir0_test_pass = 1;

extern void test_userspace_auth_policy(void);

static void (*const tests[])(void) = {
	test_userspace_auth_policy,
};

int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
		tests[i]();

	TEST_EXIT();
}
