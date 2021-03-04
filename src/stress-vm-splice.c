/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,	"vm-splice-ops N",	"stop after N bogo splice operations" },
	{ NULL,	"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_vm_splice_bytes(const char *opt)
{
	size_t vm_splice_bytes;

	vm_splice_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("vm-splice-bytes", vm_splice_bytes,
		MIN_VM_SPLICE_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("vm-splice-bytes", TYPE_ID_SIZE_T, &vm_splice_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_vm_splice_bytes,	stress_set_vm_splice_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_VMSPLICE) &&	\
    defined(SPLICE_F_MOVE)

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
static int stress_vm_splice(const stress_args_t *args)
{
	int fd, fds[2];
	uint8_t *buf;
	const size_t page_size = args->page_size;
	size_t sz, vm_splice_bytes = DEFAULT_VM_SPLICE_BYTES;
	char data[page_size];

	if (!stress_get_setting("vm-splice-bytes", &vm_splice_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vm_splice_bytes = MAX_VM_SPLICE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vm_splice_bytes = MIN_VM_SPLICE_BYTES;
	}
	vm_splice_bytes /= args->num_instances;
	if (vm_splice_bytes < MIN_VM_SPLICE_BYTES)
		vm_splice_bytes = MIN_VM_SPLICE_BYTES;
	if (vm_splice_bytes < page_size)
		vm_splice_bytes = page_size;
	sz = vm_splice_bytes & ~(page_size - 1);

	buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		int rc = exit_status(errno);

		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	}

	if (pipe(fds) < 0) {
		(void)munmap(buf, sz);
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	if ((fd = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap(buf, sz);
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	}

	stress_strnrnd(data, sizeof(data));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
		ssize_t bytes;
		struct iovec iov;

		/*
		 *  vmsplice from memory to pipe
		 */
		(void)memset(buf, 0, sz);
		iov.iov_base = buf;
		iov.iov_len = sz;
		bytes = vmsplice(fds[1], &iov, 1, 0);
		if (bytes < 0)
			break;
		ret = splice(fds[0], NULL, fd, NULL,
			vm_splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		/*
		 *  vmsplice from pipe to memory
		 */
		bytes = write(fds[1], data, sizeof(data));
		if (bytes > 0) {
			iov.iov_base = buf;
			iov.iov_len = bytes;

			bytes = vmsplice(fds[0], &iov, 1, 0);
			if (bytes < 0)
				break;
		}

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(buf, sz);
	(void)close(fd);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return EXIT_SUCCESS;
}

stressor_info_t stress_vm_splice_info = {
	.stressor = stress_vm_splice,
	.class = CLASS_VM | CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_vm_splice_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
