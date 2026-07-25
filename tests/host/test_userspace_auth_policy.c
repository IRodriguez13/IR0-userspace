/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_userspace_auth_policy.c
 * Description: Host tests — userspace account policy (passwd/shadow/group).
 *              Runs against lib/ir0_auth.c with /etc redirected to a scratch dir.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../lib/ir0_auth.h"

#define TEST_DIR IR0_AUTH_TEST_DIR

static void write_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return;
	fputs(content, f);
	fclose(f);
}

static void seed_files(void)
{
	(void)mkdir(TEST_DIR, 0700);
	write_file(TEST_DIR "/passwd",
		   "root:x:0:0:root:/root:/bin/sh\n"
		   "ivan:x:1000:100:Ivan:/home/ivan:/bin/sh\n"
		   "daemonuser:x:70:70::/:/bin/false\n");
	write_file(TEST_DIR "/group",
		   "root:x:0:\n"
		   "wheel:x:10:ivan\n"
		   "users:x:100:ivan\n"
		   "nogroup:x:99:\n");
	write_file(TEST_DIR "/shadow",
		   "root:!:0:0:99999:7:::\n"
		   "ivan:*:1:2:99999:7:::\n"
		   "daemonuser::0:0:99999:7:::\n");
}

void test_userspace_auth_policy(void)
{
	struct ir0_account acct;
	char hash[IR0_AUTH_HASH_MAX];
	char stored[IR0_AUTH_HASH_MAX];
	gid_t groups[IR0_AUTH_GROUPS_MAX];
	int ngroups;

	TEST_BEGIN("userspace_auth_policy");
	seed_files();

	/* passwd(5) parsing, including the shell/home defaults. */
	ASSERT(ir0_account_by_name("ivan", &acct) == 0);
	ASSERT(acct.uid == 1000);
	ASSERT(acct.gid == 100);
	ASSERT(strcmp(acct.home, "/home/ivan") == 0);
	ASSERT(strcmp(acct.shell, "/bin/sh") == 0);
	ASSERT(ir0_account_by_uid(0, &acct) == 0);
	ASSERT(strcmp(acct.name, "root") == 0);
	ASSERT(ir0_account_by_name("nosuchuser", &acct) != 0);

	/* Locked accounts never authenticate, not even with an empty password. */
	ASSERT(ir0_hash_is_locked("!"));
	ASSERT(ir0_hash_is_locked("!!"));
	ASSERT(ir0_hash_is_locked("*"));
	ASSERT(!ir0_hash_is_locked("$6$salt$hash"));
	ASSERT(!ir0_password_verify("!", ""));
	ASSERT(!ir0_password_verify("!", "root"));
	ASSERT(!ir0_password_verify("*", ""));
	/* An unhashed field is not a plaintext password. */
	ASSERT(!ir0_password_verify("secret", "secret"));
	/* Empty field means "no password" (development profiles only). */
	ASSERT(ir0_password_verify("", ""));
	ASSERT(!ir0_password_verify("", "x"));

	/* SHA-512 crypt with a fresh salt. */
	ASSERT(ir0_password_hash("s3cret", hash, sizeof(hash)) == 0);
	ASSERT(strncmp(hash, "$6$", 3) == 0);
	ASSERT(ir0_password_verify(hash, "s3cret"));
	ASSERT(!ir0_password_verify(hash, "s3cret "));
	ASSERT(!ir0_password_verify(hash, ""));
	{
		char other[IR0_AUTH_HASH_MAX];

		ASSERT(ir0_password_hash("s3cret", other, sizeof(other)) == 0);
		/* Random salt: the same password must not hash identically. */
		ASSERT(strcmp(hash, other) != 0);
	}

	/* Atomic shadow update keeps the other entries and the aging fields. */
	ASSERT(ir0_shadow_hash("ivan", stored, sizeof(stored)) == 0);
	ASSERT(strcmp(stored, "*") == 0);
	ASSERT(ir0_shadow_set_hash("ivan", hash) == 0);
	ASSERT(ir0_shadow_hash("ivan", stored, sizeof(stored)) == 0);
	ASSERT(strcmp(stored, hash) == 0);
	ASSERT(ir0_password_verify(stored, "s3cret"));
	ASSERT(ir0_shadow_hash("root", stored, sizeof(stored)) == 0);
	ASSERT(strcmp(stored, "!") == 0);
	{
		FILE *f = fopen(TEST_DIR "/shadow", "r");
		char line[512];
		int saw_aging = 0;

		ASSERT(f != NULL);
		while (f && fgets(line, sizeof(line), f))
		{
			if (strncmp(line, "ivan:", 5) == 0 &&
			    strstr(line, ":1:2:99999:7:::"))
				saw_aging = 1;
		}
		if (f)
			fclose(f);
		ASSERT(saw_aging);
	}
	/* A missing account gets appended, not silently dropped. */
	ASSERT(ir0_shadow_set_hash("newuser", hash) == 0);
	ASSERT(ir0_shadow_hash("newuser", stored, sizeof(stored)) == 0);
	ASSERT(strcmp(stored, hash) == 0);

	/* Login groups: primary first, then every supplementary membership. */
	ngroups = ir0_group_list("ivan", 100, groups, IR0_AUTH_GROUPS_MAX);
	ASSERT(ngroups == 2);
	ASSERT(groups[0] == 100);
	ASSERT(groups[1] == 10);
	ASSERT(ir0_user_in_group("ivan", "wheel"));
	ASSERT(!ir0_user_in_group("root", "wheel"));
	ASSERT(!ir0_user_in_group("ivan", "nogroup"));
	ngroups = ir0_group_list("root", 0, groups, IR0_AUTH_GROUPS_MAX);
	ASSERT(ngroups == 1);
	ASSERT(groups[0] == 0);

	(void)unlink(TEST_DIR "/passwd");
	(void)unlink(TEST_DIR "/shadow");
	(void)unlink(TEST_DIR "/group");
	(void)unlink(TEST_DIR "/.pwd.lock");
	(void)rmdir(TEST_DIR);
	TEST_END();
}
