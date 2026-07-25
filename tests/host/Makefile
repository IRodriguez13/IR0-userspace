# SPDX-License-Identifier: GPL-3.0-only
# IR0-userspace host tests: account policy (passwd/shadow/group) built with the
# host libc. No kernel headers are used; the boundary is UAPI only.

ROOT ?= ../..
CC = gcc
CFLAGS = -Wall -Wextra -g -I.
LDFLAGS = -lcrypt

# ir0_auth reads /etc by default; redirect it to a scratch dir so the tests
# exercise real file updates without touching the host account database.
AUTH_TEST_DIR = /tmp/ir0-auth-host-test
AUTH_DEFS = -DIR0_AUTH_TEST_DIR='"$(AUTH_TEST_DIR)"' \
	-DIR0_PASSWD_FILE='"$(AUTH_TEST_DIR)/passwd"' \
	-DIR0_SHADOW_FILE='"$(AUTH_TEST_DIR)/shadow"' \
	-DIR0_GROUP_FILE='"$(AUTH_TEST_DIR)/group"' \
	-DIR0_LOCK_FILE='"$(AUTH_TEST_DIR)/.pwd.lock"'

OBJS = test_runner.o test_userspace_auth_policy.o ir0_auth.o
TARGET = userspace_tests

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CC) -o $@ $(OBJS) $(LDFLAGS)

ir0_auth.o: $(ROOT)/lib/ir0_auth.c
	@$(CC) $(CFLAGS) $(AUTH_DEFS) -c -o $@ $<

test_userspace_auth_policy.o: test_userspace_auth_policy.c
	@$(CC) $(CFLAGS) $(AUTH_DEFS) -c -o $@ $<

test_runner.o: test_runner.c
	@$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	@rm -rf $(AUTH_TEST_DIR)
	@./$(TARGET)

clean:
	@rm -f $(OBJS) $(TARGET)
	@rm -rf $(AUTH_TEST_DIR)
