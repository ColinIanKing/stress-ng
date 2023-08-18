// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"null N",	"start N workers writing to /dev/null" },
	{ NULL,	"null-ops N",	"stop after N /dev/null bogo write operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_null
 *	stress writing to /dev/null
 */
static int stress_null(const stress_args_t *args)
{
	int fd;
	char ALIGN64 buffer[4096];
	int fcntl_mask = 0;
	double duration = 0.0, bytes = 0.0, rate;
	int metrics_count = 0;

#if defined(O_APPEND)
	fcntl_mask |= O_APPEND;
#endif
#if defined(O_ASYNC)
	fcntl_mask |= O_ASYNC;
#endif
#if defined(O_NONBLOCK)
	fcntl_mask |= O_NONBLOCK;
#endif
	if ((fd = open("/dev/null", O_RDWR)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)shim_memset(buffer, 0xff, sizeof(buffer));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;
		ssize_t ret;
		int flag;
#if defined(__linux__)
		void *ptr;
		const size_t page_size = args->page_size;
#endif

		if (UNLIKELY(metrics_count == 0))
			t = stress_time_now();
		ret = write(fd, buffer, sizeof(buffer));
		if (UNLIKELY(ret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			continue;
		} else {
			if (UNLIKELY(metrics_count == 0)) {
				duration += stress_time_now() - t;
				bytes += (double)ret;
			}
		}
		if (metrics_count++ > 100)
			metrics_count = 0;

		VOID_RET(off_t, lseek(fd, (off_t)0, SEEK_SET));
		VOID_RET(off_t, lseek(fd, (off_t)0, SEEK_END));
		VOID_RET(off_t, lseek(fd, (off_t)stress_mwc64(), SEEK_CUR));

		/* Illegal fallocate, should return ENODEV */
		VOID_RET(int, shim_fallocate(fd, 0, 0, 4096));

		/* Fdatasync, EINVAL? */
		VOID_RET(int, shim_fdatasync(fd));

		flag = fcntl(fd, F_GETFL, 0);
		if (flag >= 0) {
			const int newflag = O_RDWR | ((int)stress_mwc32() & fcntl_mask);

			VOID_RET(int, fcntl(fd, F_SETFL, newflag));
			VOID_RET(int, fcntl(fd, F_SETFL, flag));
		}

#if defined(FIGETBSZ)
		{
			int isz;

			VOID_RET(int, ioctl(fd, FIGETBSZ, &isz));
		}
#endif

#if defined(FIONREAD)
		{
			int isz = 0;

			/* Should return -ENOTTY for /dev/null */
			VOID_RET(int, ioctl(fd, FIONREAD, &isz));
		}
#endif

#if defined(__linux__)
		{
			const off_t off = (off_t)stress_mwc64() & ~((off_t)page_size - 1);
			ptr = mmap(NULL, page_size, PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, fd, off);
			if (ptr != MAP_FAILED) {
				(void)shim_memset(ptr, stress_mwc8(), page_size);
				(void)shim_msync(ptr, page_size, MS_SYNC);
				(void)munmap(ptr, page_size);
			}
		}
#endif

		stress_bogo_inc(args);
	} while (stress_continue(args));
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? (bytes / duration) / (double)MB : 0.0;
	stress_metrics_set(args, 0, "MB per sec /dev/null write rate", rate);

	return EXIT_SUCCESS;
}

stressor_info_t stress_null_info = {
	.stressor = stress_null,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
