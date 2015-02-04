/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>

#if defined(__linux__)

#include "stress-ng.h"

typedef struct {
	void *addr;	/* Buffer to read/write to */
	uint8_t val;	/* Value to check */
} addr_msg_t;

static size_t opt_vm_rw_bytes = DEFAULT_VM_RW_BYTES;

void stress_set_vm_rw_bytes(const char *optarg)
{
	opt_vm_rw_bytes = (size_t)get_uint64_byte(optarg);
	check_range("vm-rw-bytes", opt_vm_rw_bytes,
		MIN_VM_RW_BYTES, MAX_VM_RW_BYTES);
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
	pid_t pid;
	int pipe_wr[2], pipe_rd[2];
	const size_t page_size = stress_get_pagesize();
	const size_t sz = opt_vm_rw_bytes & ~(page_size - 1);

	(void)instance;

	if (pipe(pipe_wr) < 0) {
		pr_failed_dbg(name, "pipe");
		return EXIT_FAILURE;
	}
	if (pipe(pipe_rd) < 0) {
		(void)close(pipe_wr[0]);
		(void)close(pipe_wr[1]);
		pr_failed_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipe_wr[0]);
		(void)close(pipe_wr[1]);
		(void)close(pipe_rd[0]);
		(void)close(pipe_rd[1]);
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child */
		uint8_t *buf;
		int ret = EXIT_SUCCESS;
		addr_msg_t msg_rd, msg_wr;

		/* Close unwanted ends */
		(void)close(pipe_wr[0]);
		(void)close(pipe_rd[1]);

		buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (buf == MAP_FAILED) {
			pr_failed_dbg(name, "mmap");
			ret = EXIT_FAILURE;
			goto cleanup;
		}

		for (;;) {
			uint8_t *ptr, *end = buf + sz;
			int ret;

			msg_wr.addr = buf;
			msg_wr.val = 0;

			/* Send address of buffer to parent */
			ret = write(pipe_wr[1], &msg_wr, sizeof(msg_wr));
			if (ret < 0) {
				if (errno != EBADF)
					pr_failed_dbg(name, "write");
				break;
			}

			/* Wait for parent to populate data */
			ret = read(pipe_rd[0], &msg_rd, sizeof(msg_rd));
			if (ret == 0)
				break;
			if (ret != sizeof(msg_rd)) {
				pr_failed_dbg(name, "read");
				break;
			}

			if (opt_flags & OPT_FLAGS_VERIFY) {
				/* Check memory altered by parent is sane */
				for (ptr = buf; ptr < end; ptr += page_size) {
					if (*ptr != msg_rd.val) {
						pr_fail(stderr, "%s: memory at %p: %d vs %d\n",
							name, ptr, *ptr, msg_rd.val);
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
		if (write(pipe_wr[1], &msg_wr, sizeof(msg_wr)) <= 0) {
			if (errno != EBADF)
				pr_dbg(stderr, "%s: failed to write termination message "
					"over pipe: errno=%d (%s)\n",
					name, errno, strerror(errno));
		}

		(void)close(pipe_wr[0]);
		(void)close(pipe_wr[1]);
		(void)close(pipe_rd[0]);
		(void)close(pipe_rd[1]);
		(void)munmap(buf, sz);
		exit(ret);
	} else {
		/* Parent */
		int status;
		uint8_t val = 0;
		uint8_t *localbuf;
		addr_msg_t msg_rd, msg_wr;

		localbuf = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (localbuf == MAP_FAILED) {
			(void)close(pipe_wr[0]);
			(void)close(pipe_wr[1]);
			(void)close(pipe_rd[0]);
			(void)close(pipe_rd[1]);
			pr_failed_dbg(name, "mmap");
			exit(EXIT_FAILURE);
		}

		/* Close unwanted ends */
		(void)close(pipe_wr[1]);
		(void)close(pipe_rd[0]);

		do {
			struct iovec local[1], remote[1];
			uint8_t *ptr, *end = localbuf + sz;
			int ret;

			/* Wait for address of child's buffer */
			ret = read(pipe_wr[0], &msg_rd, sizeof(msg_rd));
			if (ret == 0)
				break;
			if (ret != sizeof(msg_rd)) {
				pr_failed_dbg(name, "read");
				break;
			}
			/* Child telling us it's terminating? */
			if (!msg_rd.addr)
				break;

			/* Perform read from child's memory */
			local[0].iov_base = localbuf;
			local[0].iov_len = sz;
			remote[0].iov_base = msg_rd.addr;
			remote[0].iov_len = sz;
			if (process_vm_readv(pid, local, 1, remote, 1, 0) < 0) {
				pr_failed_dbg(name, "process_vm_readv");
				break;
			}

			if (opt_flags & OPT_FLAGS_VERIFY) {
				/* Check data is sane */
				for (ptr = localbuf; ptr < end; ptr += page_size) {
					if (*ptr) {
						pr_fail(stderr, "%s: memory at %p: %d vs %d\n",
							name, ptr, *ptr, msg_rd.val);
						goto fail;
					}
					*ptr = 0;
				}
				/* Set memory */
				for (ptr = localbuf; ptr < end; ptr += page_size)
					*ptr = val;
			}

			/* Write to child's memory */
			msg_wr = msg_rd;
			local[0].iov_base = localbuf;
			local[0].iov_len = sz;
			remote[0].iov_base = msg_rd.addr;
			remote[0].iov_len = sz;
			if (process_vm_writev(pid, local, 1, remote, 1, 0) < 0) {
				pr_failed_dbg(name, "process_vm_writev");
				break;
			}
			msg_wr.val = val;
			val++;

			/* Inform child that memory has been changed */
			ret = write(pipe_rd[1], &msg_wr, sizeof(msg_wr));
			if (ret < 0) {
				if (errno != EBADF)
					pr_failed_dbg(name, "write");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
fail:
		/* Tell child we're done */
		msg_wr.addr = NULL;
		msg_wr.val = 0;
		if (write(pipe_wr[0], &msg_wr, sizeof(msg_wr)) < 0) {
			if (errno != EBADF)
				pr_dbg(stderr, "%s: failed to write termination message "
					"over pipe: errno=%d (%s)\n",
					name, errno, strerror(errno));
		}
		(void)close(pipe_wr[0]);
		(void)close(pipe_wr[1]);
		(void)close(pipe_rd[0]);
		(void)close(pipe_rd[1]);
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		(void)munmap(localbuf, sz);
	}

	return EXIT_SUCCESS;
}

#endif
