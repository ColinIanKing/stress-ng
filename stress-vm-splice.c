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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "stress-ng.h"

#if defined (__linux__)

static size_t opt_vm_splice_bytes = DEFAULT_VM_SPLICE_BYTES;

void stress_set_vm_splice_bytes(const char *optarg)
{
	opt_vm_splice_bytes = (size_t)get_uint64_byte(optarg);
	check_range("vm-splice-bytes", opt_vm_splice_bytes,
		MIN_VM_SPLICE_BYTES, MAX_VM_SPLICE_BYTES);
}

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
int stress_vm_splice(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, fds[2];
	uint8_t *buf;
	const size_t page_size = stress_get_pagesize();
	const size_t sz = opt_vm_splice_bytes & ~(page_size - 1);

	(void)instance;

	buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_failed_dbg(name, "mmap");
		return(EXIT_FAILURE);
	}

	if (pipe(fds) < 0) {
		(void)munmap(buf, sz);
		pr_failed_err(name, "pipe");
		return EXIT_FAILURE;
	}

	if ((fd = open("/dev/null", O_WRONLY)) < 0) {
		(void)munmap(buf, sz);
		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}

	do {
		int ret;
		ssize_t bytes;
		struct iovec iov;

		iov.iov_base = buf;
		iov.iov_len = sz;

		bytes = vmsplice(fds[1], &iov, 1, 0);
		if (bytes < 0)
			break;
		ret = splice(fds[0], NULL, fd, NULL, opt_vm_splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)munmap(buf, sz);
	(void)close(fd);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return EXIT_SUCCESS;
}

#endif
