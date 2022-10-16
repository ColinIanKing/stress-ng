/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
 */
#include "stress-ng.h"

#define MIN_VM_SPLICE_BYTES	(4 * KB)
#define MAX_VM_SPLICE_BYTES	(64 * MB)
#define DEFAULT_VM_SPLICE_BYTES	(64 * KB)

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
	char *data;
	double duration = 0.0, bytes = 0.0, vm_splices = 0.0, rate;

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
		pr_inf_skip("%s: mmap of %zd sized buffer failed, errno=%d (%s), skipping stressor\n",
			args->name, sz, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	data = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zd sized buffer failed, errno=%d (%s), skipping stressor\n",
			args->name, page_size, errno, strerror(errno));
		(void)munmap(buf, sz);
		return EXIT_NO_RESOURCE;
	}

	if (pipe(fds) < 0) {
		(void)munmap((void *)data, page_size);
		(void)munmap((void *)buf, sz);
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)data, page_size);
		(void)munmap((void *)buf, sz);
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	}

	stress_strnrnd(data, page_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret, n_bytes;
		struct iovec iov;
		double t;

		/*
		 *  vmsplice from memory to pipe
		 */
		(void)memset(buf, 0, sz);
		iov.iov_base = buf;
		iov.iov_len = sz;
		t = stress_time_now();
		n_bytes = vmsplice(fds[1], &iov, 1, 0);
		if (UNLIKELY(n_bytes < 0))
			break;
		duration += stress_time_now() - t;
		bytes += (double)n_bytes;
		vm_splices += 1.0;
		
		ret = splice(fds[0], NULL, fd, NULL,
			vm_splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		/*
		 *  vmsplice from pipe to memory
		 */
		n_bytes = write(fds[1], data, page_size);
		if (n_bytes > 0) {
			iov.iov_base = buf;
			iov.iov_len = (size_t)n_bytes;

			t = stress_time_now();
			n_bytes = vmsplice(fds[0], &iov, 1, 0);
			if (n_bytes < 0)
				break;
			duration += stress_time_now() - t;
			bytes += (double)n_bytes;
			vm_splices += 1.0;
		}

		inc_counter(args);
	} while (keep_stressing(args));

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "MB vm-splice'd per second", rate / (double)MB);
	rate = (duration > 0.0) ? vm_splices / duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 1, "vm-splice calls per second", rate);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)data, page_size);
	(void)munmap((void *)buf, sz);
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
