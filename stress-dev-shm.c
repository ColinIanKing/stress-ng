/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"dev-shm N",	"start N /dev/shm file and mmap stressors" },
	{ NULL,	"dev-shm-ops N","stop after N /dev/shm bogo ops" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

/*
 *  stress_dev_shm_child()
 * 	stress /dev/shm by filling it with data and mmap'ing
 *	to it once we hit the largest file size allowed.
 */
static inline int stress_dev_shm_child(
	const args_t *args,
	const int fd)
{
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	const size_t page_thresh = 16 * MB;
	ssize_t sz = page_size;
	uint32_t *addr;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(args->name, true);

	while (keep_stressing()) {
		size_t sz_delta = page_thresh;
		int ret;

		ret = ftruncate(fd, 0);
		if (ret < 0) {
			pr_err("%s: ftruncate failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		/*
		 *  Try to allocate the largest file size
		 *  possible using a fast rough binary search. We
		 *  shouldn't make this exact as mmap'ing this
		 *  can trip a SIGBUS
		 */
		while (keep_stressing() && (sz_delta >= page_thresh)) {
			ret = shim_fallocate(fd, 0, 0, sz);
			if (ret < 0) {
				sz -= (sz_delta >> 1);
				break;
			} else {
				sz += sz_delta;
				sz_delta <<= 1;
				inc_counter(args);
			}
		}
		if (sz > 0) {
			/*
			 *  Now try to map this into our address space
			 */
			if (!keep_stressing())
				break;
			addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
			if (addr != MAP_FAILED)  {
				const size_t words = page_size / sizeof(uint32_t);
				uint32_t *ptr, *end = addr + (sz / sizeof(uint32_t));

				(void)madvise_random(addr, sz);

				/* Touch all pages with random data */
				for (ptr = addr; ptr < end; ptr += words) {
					*ptr = mwc32();
				}
				(void)msync(addr, sz, MS_INVALIDATE);
				(void)munmap(addr, sz);
			}
			sz = page_size;
			ret = ftruncate(fd, 0);
			if (ret < 0) {
				pr_err("%s: ftruncate failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
	}
	return rc;
}

/*
 *  stress_dev_shm()
 *	stress /dev/shm
 */
static int stress_dev_shm(const args_t *args)
{
	int fd, rc = EXIT_SUCCESS;
	char path[PATH_MAX];
	pid_t pid;

	/*
	 *  Sanity check for existence and r/w permissions
	 *  on /dev/shm, it may not be configure for the
	 *  kernel, so don't make it a failure of it does
	 *  not exist or we can't access it.
	 */
	if (access("/dev/shm", R_OK | W_OK) < 0) {
		if (errno == ENOENT) {
			pr_inf("%s: /dev/shm does not exist, skipping test\n",
				args->name);
			return EXIT_NO_RESOURCE;
		} else {
			pr_inf("%s: cannot access /dev/shm, errno=%d (%s), skipping test\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}

	(void)snprintf(path, sizeof(path), "/dev/shm/stress-dev-shm-%d-%d-%" PRIu32,
		args->instance, getpid(), mwc32());
	fd = open(path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf("%s: cannot create %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return EXIT_SUCCESS;
	}
	(void)unlink(path);

	while (keep_stressing()) {
fork_again:
		pid = fork();
		if (pid < 0) {
			/* Can't fork, retry? */
			if ((errno == EAGAIN) || (errno == ENOMEM))
				goto fork_again;
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			/* Nope, give up! */
			(void)close(fd);
			return EXIT_FAILURE;
		} else if (pid > 0) {
			/* Parent */
			int ret, status = 0;

			(void)setpgid(pid, g_pgrp);
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			}
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGKILL)) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						args->name, args->instance);
				}
			}
		} else if (pid == 0) {
			/* Child, stress memory */
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			rc = stress_dev_shm_child(args, fd);
			_exit(rc);
		}
	}
	(void)close(fd);
	return rc;
}

stressor_info_t stress_dev_shm_info = {
	.stressor = stress_dev_shm,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_dev_shm_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#endif
