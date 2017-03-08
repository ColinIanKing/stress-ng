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

#define MAX_MEM_FDS 	(256)

static size_t opt_memfd_bytes = DEFAULT_MEMFD_BYTES;
static bool set_memfd_bytes;

void stress_set_memfd_bytes(const char *optarg)
{
	set_memfd_bytes = true;
	opt_memfd_bytes = (size_t)
		get_uint64_byte_memory(optarg,
			stressor_instances(STRESS_MEMFD));
	check_range_bytes("memfd-bytes", opt_memfd_bytes,
		MIN_MEMFD_BYTES, MAX_MEM_LIMIT);
}

#if defined(__linux__) && defined(__NR_memfd_create)

/*
 *  Create allocations using memfd_create, ftruncate and mmap
 */
static void stress_memfd_allocs(const args_t *args)
{
	int fds[MAX_MEM_FDS];
	void *maps[MAX_MEM_FDS];
	size_t i;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t size = opt_memfd_bytes / MAX_MEM_FDS;

	if (size < min_size)
		size = min_size;

	do {
		for (i = 0; i < MAX_MEM_FDS; i++) {
			fds[i] = -1;
			maps[i] = MAP_FAILED;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
			char filename[PATH_MAX];

			(void)snprintf(filename, sizeof(filename), "memfd-%u-%zu", args->pid, i);
			fds[i] = shim_memfd_create(filename, 0);
			if (fds[i] < 0) {
				switch (errno) {
				case EMFILE:
				case ENFILE:
					break;
				case ENOMEM:
					goto clean;
				case ENOSYS:
				case EFAULT:
				default:
					pr_err("%s: memfd_create failed: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					g_keep_stressing_flag = false;
					goto clean;
				}
			}
			if (!g_keep_stressing_flag)
				goto clean;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
			if (fds[i] >= 0) {
				ssize_t ret;
				size_t whence;

				if (!g_keep_stressing_flag)
					break;

				/* Allocate space */
				ret = ftruncate(fds[i], size);
				if (ret < 0) {
					switch (errno) {
					case EINTR:
						break;
					default:
						pr_fail_err("ftruncate");
						break;
					}
				}
				/*
				 * ..and map it in, using MAP_POPULATE
				 * to force page it in
				 */
				maps[i] = mmap(NULL, size, PROT_WRITE,
					MAP_FILE | MAP_SHARED | MAP_POPULATE,
					fds[i], 0);
				mincore_touch_pages(maps[i], size);
				madvise_random(maps[i], size);

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
				/*
				 *  ..and punch a hole
				 */
				whence = (mwc32() % size) & ~(page_size - 1);
				ret = shim_fallocate(fds[i], FALLOC_FL_PUNCH_HOLE |
					FALLOC_FL_KEEP_SIZE, whence, page_size);
				(void)ret;
#endif
			}
			if (!g_keep_stressing_flag)
				goto clean;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
#if defined(SEEK_SET)
			if (lseek(fds[i], (off_t)size>> 1, SEEK_SET) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_SET on memfd");
			}
#endif
#if defined(SEEK_CUR)
			if (lseek(fds[i], (off_t)0, SEEK_CUR) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_CUR on memfd");
			}
#endif
#if defined(SEEK_END)
			if (lseek(fds[i], (off_t)0, SEEK_END) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_END on memfd");
			}
#endif
#if defined(SEEK_HOLE)
			if (lseek(fds[i], (off_t)0, SEEK_HOLE) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_HOLE on memfd");
			}
#endif
#if defined(SEEK_DATA)
			if (lseek(fds[i], (off_t)0, SEEK_DATA) < 0) {
				if (errno != ENXIO)
					pr_fail_err("lseek SEEK_DATA on memfd");
			}
#endif
			if (!g_keep_stressing_flag)
				goto clean;
		}
clean:
		for (i = 0; i < MAX_MEM_FDS; i++) {
			if (maps[i] != MAP_FAILED)
				(void)munmap(maps[i], size);
			if (fds[i] >= 0)
				(void)close(fds[i]);
		}
		inc_counter(args);
	} while (keep_stressing());
}


/*
 *  stress_memfd()
 *	stress memfd
 */
int stress_memfd(const args_t *args)
{
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, nomems = 0;

	if (!set_memfd_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_memfd_bytes = MAX_MEMFD_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_memfd_bytes = MIN_MEMFD_BYTES;
	}

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		stress_parent_died_alarm();

		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)), args->instance);
			/* If we got killed by OOM killer, re-start */
			if ((WTERMSIG(status) == SIGKILL) ||
			    (WTERMSIG(status) == SIGSEGV)) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				ooms++;
				goto again;
			}

		} else if (WTERMSIG(status) == SIGSEGV) {
			pr_dbg("%s: killed by SIGSEGV, "
				"restarting again "
				"(instance %d)\n",
				args->name, args->instance);
			segvs++;
			goto again;
		}
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		stress_memfd_allocs(args);
	}

	if (ooms + segvs + nomems > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			args->name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}
#else
int stress_memfd(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
