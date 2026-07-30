/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ir0_keymap.h
 * Description: Userspace keyboard layout switch (IR0 keymap_set/get).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_KEYMAP_H
#define IR0_KEYMAP_H

#define IR0_KEYMAP_FILE "/etc/keymap"

/* Match includes/ir0/input_backend.h / KEYBOARD_LAYOUT_*. */
#define IR0_KBD_LAYOUT_US    0
#define IR0_KBD_LAYOUT_LATAM 1

const char *ir0_keymap_name(int layout);
int ir0_keymap_parse(const char *s);
int ir0_keymap_get(void);
int ir0_keymap_set(int layout);
/* Apply /etc/keymap (or @path). Returns 0 applied, 1 missing, -1 error. */
int ir0_keymap_apply_file(const char *path);
int ir0_keymap_write_file(const char *path, int layout);

#endif /* IR0_KEYMAP_H */
