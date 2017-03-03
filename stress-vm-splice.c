/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include "stress-ng.h"

static size_t opt_vm_splice_bytes = DEFAULT_VM_SPLICE_BYTES;
static bool set_vm_splice_bytes = false;

void stress_set_vm_splice_bytes(const char *optarg)
{
	set_vm_splice_bytes = true;
	opt_vm_splice_bytes = (size_t)
		get_uint64_byte_memory(optarg,
			stressor_instances(STRESS_VM_SPLICE));
	check_range_bytes("vm-splice-bytes", opt_vm_splice_bytes,
		MIN_VM_SPLICE_BYTES, MAX_MEM_LIMIT);
}

#if defined(__linux__) && NEED_GLIBC(2,5,0)

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
int stress_vm_splice(const args_t *args)
{
	int fd, fds[2];
	uint8_t *buf;
	const size_t page_size = args->page_size;
	size_t sz;

	if (!set_vm_splice_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_vm_splice_bytes = MAX_VM_SPLICE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_vm_splice_bytes = MIN_VM_SPLICE_BYTES;
	}
	sz = opt_vm_splice_bytes & ~(page_size - 1);

	buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		int rc = exit_status(errno);

		pr_fail_dbg("mmap");
		return rc;
	}

	if (pipe(fds) < 0) {
		(void)munmap(buf, sz);
		pr_fail_err("pipe");
		return EXIT_FAILURE;
	}

	if ((fd = open("/dev/null", O_WRONLY)) < 0) {
		(void)munmap(buf, sz);
		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_fail_err("open");
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
		ret = splice(fds[0], NULL, fd, NULL,
			opt_vm_splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		inc_counter(args);
	} while (keep_stressing());

	(void)munmap(buf, sz);
	(void)close(fd);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return EXIT_SUCCESS;
}
#else
int stress_vm_splice(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
