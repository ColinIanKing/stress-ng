/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"dev-shm N",	"start N /dev/shm file and mmap stressors" },
	{ NULL,	"dev-shm-ops N","stop after N /dev/shm bogo ops" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

typedef struct {
	int fd;			/* /dev/shm File descriptor */
} stress_dev_shm_context_t;

/*
 *  stress_dev_shm_child()
 * 	stress /dev/shm by filling it with data and mmap'ing
 *	to it once we hit the largest file size allowed.
 */
static inline int stress_dev_shm_child(
	stress_args_t *args,
	const int fd)
{
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	const size_t page_thresh = 16 * MB;
	ssize_t sz = (ssize_t)page_size;
	uint32_t *addr;

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	while (stress_continue(args)) {
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
		while (stress_continue(args) && (sz_delta >= page_thresh)) {
			ret = shim_fallocate(fd, 0, 0, (off_t)sz);
			if (ret < 0) {
				sz -= (sz_delta >> 1);
				break;
			} else {
				sz += sz_delta;
				sz_delta <<= 1;
				stress_bogo_inc(args);
			}
		}
		if (sz > 0) {
			/*
			 *  Now try to map this into our address space
			 */
			if (UNLIKELY(!stress_continue(args)))
				break;
			addr = (uint32_t *)mmap(NULL, (size_t)sz, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, fd, 0);
			if (addr != MAP_FAILED) {
				register uint32_t *ptr;
				register const uint32_t *end = addr + ((size_t)sz / sizeof(*end));
				const size_t words = page_size / sizeof(*ptr);
				const uint32_t rnd = stress_mwc32();

				stress_set_vma_anon_name(addr, (size_t)sz, "mmapped-dev-shm");
				(void)stress_madvise_randomize(addr, (size_t)sz);
				(void)stress_madvise_mergeable(addr, (size_t)sz);

				/* Touch all pages with data */
				for (ptr = addr; ptr < end; ptr += words) {
					register const uint32_t val = (uint32_t)((uintptr_t)ptr ^ rnd) & 0xffffffffU;

					*ptr = val;
				}
				/* Verify contents */
				for (ptr = addr; ptr < end; ptr += words) {
					register const uint32_t val = (uint32_t)((uintptr_t)ptr ^ rnd) & 0xffffffffU;

					if (*ptr != val) {
						pr_fail("%s: address %p does not contain correct value, "
							"got 0x%" PRIx32 ", expecting 0x%" PRIx32 "\n",
							args->name, ptr, *ptr, val);
						(void)munmap((void *)addr, (size_t)sz);
						VOID_RET(int, ftruncate(fd, 0));
						return EXIT_FAILURE;
					}
				}
				(void)msync((void *)addr, (size_t)sz, MS_INVALIDATE);
				(void)munmap((void *)addr, (size_t)sz);

			}
			sz = (ssize_t)page_size;
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

static int stress_dev_shm_oomable_child(stress_args_t *args, void *ctxt)
{
	pid_t pid;
	int rc = EXIT_SUCCESS;
	stress_dev_shm_context_t *context = (stress_dev_shm_context_t *)ctxt;

	while (stress_continue(args)) {
again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			/* Nope, give up! */
			(void)close(context->fd);
			return EXIT_FAILURE;
		} else if (pid > 0) {
			/* Parent */
			pid_t ret;
			int status = 0;

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				stress_force_killed_bogo(args);
				(void)stress_kill_pid_wait(pid, &status);
			}
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGKILL)) {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						args->name, args->instance);
				}
			}
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				rc = WEXITSTATUS(status);
		} else {
			/* Child, stress memory */
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			rc = stress_dev_shm_child(args, context->fd);
			_exit(rc);
		}
	}
finish:
	return rc;
}

/*
 *  stress_dev_shm()
 *	stress /dev/shm
 */
static int stress_dev_shm(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	char path[PATH_MAX];
	stress_dev_shm_context_t context;

	/*
	 *  Sanity check for existence and r/w permissions
	 *  on /dev/shm, it may not be configure for the
	 *  kernel, so don't make it a failure of it does
	 *  not exist or we can't access it.
	 */
	if (access("/dev/shm", R_OK | W_OK) < 0) {
		if (errno == ENOENT) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: /dev/shm does not exist, skipping stressor\n",
					args->name);
			return EXIT_NO_RESOURCE;
		} else {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: cannot access /dev/shm, errno=%d (%s), skipping stressor\n",
					args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}

	(void)snprintf(path, sizeof(path), "/dev/shm/stress-dev-shm-%" PRIu32 "-%d-%" PRIu32,
		args->instance, getpid(), stress_mwc32());
	context.fd = open(path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
	if (context.fd < 0) {
		pr_inf("%s: cannot create %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return EXIT_SUCCESS;
	}
	(void)shim_unlink(path);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, &context, stress_dev_shm_oomable_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(context.fd);
	return rc;
}

const stressor_info_t stress_dev_shm_info = {
	.stressor = stress_dev_shm,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_dev_shm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
