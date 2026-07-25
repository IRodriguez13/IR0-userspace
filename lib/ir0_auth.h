/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_auth.h
 * Description: Userspace account policy: passwd(5), shadow(5), group(5), crypt(3).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_AUTH_H
#define IR0_AUTH_H

#include <stddef.h>
#include <sys/types.h>

#define IR0_AUTH_NAME_MAX 64
#define IR0_AUTH_PATH_MAX 128
#define IR0_AUTH_HASH_MAX 192
#define IR0_AUTH_GROUPS_MAX 32

/* Host tests override these paths to exercise the policy off the rootfs. */
#ifndef IR0_PASSWD_FILE
#define IR0_PASSWD_FILE "/etc/passwd"
#endif
#ifndef IR0_SHADOW_FILE
#define IR0_SHADOW_FILE "/etc/shadow"
#endif
#ifndef IR0_GROUP_FILE
#define IR0_GROUP_FILE  "/etc/group"
#endif
#ifndef IR0_LOCK_FILE
#define IR0_LOCK_FILE   "/etc/.pwd.lock"
#endif

struct ir0_account
{
	char name[IR0_AUTH_NAME_MAX];
	uid_t uid;
	gid_t gid;
	char home[IR0_AUTH_PATH_MAX];
	char shell[IR0_AUTH_PATH_MAX];
	/* Password field of passwd(5): "x" means the hash lives in shadow(5). */
	char passwd[IR0_AUTH_HASH_MAX];
};

/* Split a colon-separated line; @idx is 0-based. Returns 0 on success. */
int ir0_auth_field(const char *line, int idx, char *out, size_t outlen);

int ir0_account_by_name(const char *user, struct ir0_account *out);
int ir0_account_by_uid(uid_t uid, struct ir0_account *out);

/* Copy the shadow(5) hash of @user. Returns 0 on success, -1 if absent. */
int ir0_shadow_hash(const char *user, char *hash, size_t hashlen);

/* Non-zero when the stored field disables password authentication ("!", "*"). */
int ir0_hash_is_locked(const char *stored);

/*
 * Verify @password against a stored passwd/shadow field. Locked accounts always
 * fail; an empty field means "no password required" (development profiles only).
 */
int ir0_password_verify(const char *stored, const char *password);

/* Build a SHA-512 crypt ($6$) hash with a fresh random salt. */
int ir0_password_hash(const char *password, char *out, size_t outlen);

/* Replace the hash of @user in /etc/shadow atomically, under an advisory lock. */
int ir0_shadow_set_hash(const char *user, const char *hash);

/*
 * Collect the login groups of @user: primary @gid first, then every group(5)
 * entry listing @user. Returns the count, or -1 on error.
 */
int ir0_group_list(const char *user, gid_t gid, gid_t *groups, int max);

/* Non-zero when @user belongs to the group named @group. */
int ir0_user_in_group(const char *user, const char *group);

/* Read a line from stdin; @echo 0 also silences the tty (password prompts). */
int ir0_read_line(char *buf, size_t buflen, int echo);
int ir0_tty_restore_cooked(void);

/* Overwrite a buffer holding secrets before it goes out of scope. */
void ir0_wipe(void *buf, size_t len);

/* /etc/hostname without the trailing newline; falls back to "ir0". */
void ir0_hostname(char *out, size_t outlen);

#endif /* IR0_AUTH_H */
