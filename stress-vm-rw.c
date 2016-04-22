/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_VM_RW)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <errno.h>

#define STACK_SIZE	(64 * 1024)

typedef struct {
	const char *name;
	uint64_t *counter;
	uint64_t max_ops;
	size_t page_size;
	size_t sz;
	pid_t pid;
	int pipe_wr[2];
	int pipe_rd[2];
} context_t;

typedef struct {
	void *addr;	/* Buffer to read/write to */
	uint8_t val;	/* Value to check */
} addr_msg_t;

static size_t opt_vm_rw_bytes = DEFAULT_VM_RW_BYTES;
static bool set_vm_rw_bytes = false;

void stress_set_vm_rw_bytes(const char *optarg)
{
	set_vm_rw_bytes = true;
	opt_vm_rw_bytes = (size_t)get_uint64_byte(optarg);
	check_range("vm-rw-bytes", opt_vm_rw_bytes,
		MIN_VM_RW_BYTES, MAX_VM_RW_BYTES);
}

int stress_vm_child(void *arg)
{
	context_t *ctxt = (context_t *)arg;

	uint8_t *buf;
	int ret = EXIT_SUCCESS;
	addr_msg_t msg_rd, msg_wr;

	setpgid(0, pgrp);
	stress_parent_died_alarm();

	/* Close unwanted ends */
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);

	buf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		ret = exit_status(errno);
		pr_fail_dbg(ctxt->name, "mmap");
		goto cleanup;
	}

	while (opt_do_run) {
		uint8_t *ptr, *end = buf + ctxt->sz;
		int ret;

		memset(&msg_wr, 0, sizeof(msg_wr));
		msg_wr.addr = buf;
		msg_wr.val = 0;

		/* Send address of buffer to parent */
redo_wr1:
		ret = write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr1;
			if (errno != EBADF)
				pr_fail_dbg(ctxt->name, "write");
			break;
		}
redo_rd1:
		/* Wait for parent to populate data */
		ret = read(ctxt->pipe_rd[0], &msg_rd, sizeof(msg_rd));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd1;
			pr_fail_dbg(ctxt->name, "read");
			break;
		}
		if (ret == 0)
			break;
		if (ret != sizeof(msg_rd)) {
			pr_fail_dbg(ctxt->name, "read");
			break;
		}

		if (opt_flags & OPT_FLAGS_VERIFY) {
			/* Check memory altered by parent is sane */
			for (ptr = buf; ptr < end; ptr += ctxt->page_size) {
				if (*ptr != msg_rd.val) {
					pr_fail(stderr, "%s: memory at %p: %d vs %d\n",
						ctxt->name, ptr, *ptr, msg_rd.val);
					goto cleanup;
				}
				*ptr = 0;
			}
		}
	}
cleanup:
	/* Tell parent we're done */
	msg_wr.addr = 0;
	msg_wr.val = 0;
	if (write(ctxt->pipe_wr[1], &msg_wr, sizeof(msg_wr)) <= 0) {
		if (errno != EBADF)
			pr_dbg(stderr, "%s: failed to write termination message "
				"over pipe: errno=%d (%s)\n",
				ctxt->name, errno, strerror(errno));
	}

	(void)close(ctxt->pipe_wr[1]);
	(void)close(ctxt->pipe_rd[0]);
	(void)munmap(buf, ctxt->sz);
	return ret;
}

int stress_vm_parent(context_t *ctxt)
{
	/* Parent */
	int status;
	uint8_t val = 0;
	uint8_t *localbuf;
	addr_msg_t msg_rd, msg_wr;

	setpgid(ctxt->pid, pgrp);

	localbuf = mmap(NULL, ctxt->sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (localbuf == MAP_FAILED) {
		(void)close(ctxt->pipe_wr[0]);
		(void)close(ctxt->pipe_wr[1]);
		(void)close(ctxt->pipe_rd[0]);
		(void)close(ctxt->pipe_rd[1]);
		pr_fail_dbg(ctxt->name, "mmap");
		return EXIT_FAILURE;
	}

	/* Close unwanted ends */
	(void)close(ctxt->pipe_wr[1]);
	(void)close(ctxt->pipe_rd[0]);

	do {
		struct iovec local[1], remote[1];
		uint8_t *ptr, *end = localbuf + ctxt->sz;
		int ret;

		/* Wait for address of child's buffer */
redo_rd2:
		if (!opt_do_run)
			break;
		ret = read(ctxt->pipe_wr[0], &msg_rd, sizeof(msg_rd));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_rd2;
			pr_fail_dbg(ctxt->name, "read");
			break;
		}
		if (ret == 0)
			break;
		if (ret != sizeof(msg_rd)) {
			pr_fail_dbg(ctxt->name, "read");
			break;
		}
		/* Child telling us it's terminating? */
		if (!msg_rd.addr)
			break;

		/* Perform read from child's memory */
		local[0].iov_base = localbuf;
		local[0].iov_len = ctxt->sz;
		remote[0].iov_base = msg_rd.addr;
		remote[0].iov_len = ctxt->sz;
		if (process_vm_readv(ctxt->pid, local, 1, remote, 1, 0) < 0) {
			pr_fail_dbg(ctxt->name, "process_vm_readv");
			break;
		}

		if (opt_flags & OPT_FLAGS_VERIFY) {
			/* Check data is sane */
			for (ptr = localbuf; ptr < end; ptr += ctxt->page_size) {
				if (*ptr) {
					pr_fail(stderr, "%s: memory at %p: %d vs %d\n",
						ctxt->name, ptr, *ptr, msg_rd.val);
					goto fail;
				}
				*ptr = 0;
			}
			/* Set memory */
			for (ptr = localbuf; ptr < end; ptr += ctxt->page_size)
				*ptr = val;
		}

		/* Write to child's memory */
		msg_wr = msg_rd;
		local[0].iov_base = localbuf;
		local[0].iov_len = ctxt->sz;
		remote[0].iov_base = msg_rd.addr;
		remote[0].iov_len = ctxt->sz;
		if (process_vm_writev(ctxt->pid, local, 1, remote, 1, 0) < 0) {
			pr_fail_dbg(ctxt->name, "process_vm_writev");
			break;
		}
		msg_wr.val = val;
		val++;
redo_wr2:
		if (!opt_do_run)
			break;
		/* Inform child that memory has been changed */
		ret = write(ctxt->pipe_rd[1], &msg_wr, sizeof(msg_wr));
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr2;
			if (errno != EBADF)
				pr_fail_dbg(ctxt->name, "write");
			break;
		}
		(*ctxt->counter)++;
	} while (opt_do_run && (!ctxt->max_ops || *ctxt->counter < ctxt->max_ops));
fail:
	/* Tell child we're done */
	msg_wr.addr = NULL;
	msg_wr.val = 0;
	if (write(ctxt->pipe_wr[0], &msg_wr, sizeof(msg_wr)) < 0) {
		if (errno != EBADF)
			pr_dbg(stderr, "%s: failed to write "
				"termination message "
				"over pipe: errno=%d (%s)\n",
				ctxt->name, errno, strerror(errno));
	}
	(void)close(ctxt->pipe_wr[0]);
	(void)close(ctxt->pipe_rd[1]);
	(void)kill(ctxt->pid, SIGKILL);
	(void)waitpid(ctxt->pid, &status, 0);
	(void)munmap(localbuf, ctxt->sz);

	return EXIT_SUCCESS;
}

/*
 *  stress_vm_rw
 *	stress vm_read_v/vm_write_v
 */
int stress_vm_rw(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	context_t ctxt;
	uint8_t stack[64*1024];
	const ssize_t stack_offset =
		stress_get_stack_direction(&ctxt) * (STACK_SIZE - 64);
	uint8_t *stack_top = stack + stack_offset;

	(void)instance;

	if (!set_vm_rw_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_vm_rw_bytes = MAX_VM_RW_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_vm_rw_bytes = MIN_VM_RW_BYTES;
	}
	ctxt.name = name;
	ctxt.page_size = stress_get_pagesize();
	ctxt.sz = opt_vm_rw_bytes & ~(ctxt.page_size - 1);
	ctxt.counter = counter;
	ctxt.max_ops = max_ops;

	if (pipe(ctxt.pipe_wr) < 0) {
		pr_fail_dbg(name, "pipe");
		return EXIT_FAILURE;
	}
	if (pipe(ctxt.pipe_rd) < 0) {
		(void)close(ctxt.pipe_wr[0]);
		(void)close(ctxt.pipe_wr[1]);
		pr_fail_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	ctxt.pid = clone(stress_vm_child, align_stack(stack_top),
		SIGCHLD | CLONE_VM, &ctxt);
	if (ctxt.pid < 0) {
		(void)close(ctxt.pipe_wr[0]);
		(void)close(ctxt.pipe_wr[1]);
		(void)close(ctxt.pipe_rd[0]);
		(void)close(ctxt.pipe_rd[1]);
		pr_fail_dbg(name, "clone");
		return EXIT_FAILURE;
	}
	return stress_vm_parent(&ctxt);
}

#endif
