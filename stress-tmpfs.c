/*
 * Copyright (C) 2017 Canonical, Ltd.
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

#if defined (__linux__)
#include <sys/vfs.h>

#define MAX_MOUNTS		(256)
#define NO_MEM_RETRIES_MAX	(256)
#define TMPFS_MAGIC		0x01021994

/* Misc randomly chosen mmap flags */
static const int mmap_flags[] = {
#if defined(MAP_HUGE_2MB) && defined(MAP_HUGETLB)
	MAP_HUGE_2MB | MAP_HUGETLB,
#endif
#if defined(MAP_HUGE_1GB) && defined(MAP_HUGETLB)
	MAP_HUGE_1GB | MAP_HUGETLB,
#endif
#if defined(MAP_NONBLOCK)
	MAP_NONBLOCK,
#endif
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
	0
};

/*
 *  stress_tmpfs_open()
 *	attempts to find a writeable tmpfs file system and opens
 *	a tmpfs temp file. The file is unlinked so the final close
 *	will enforce and automatic space reap if the child process
 *	exits prematurely.
 */
static int stress_tmpfs_open(const args_t *args, off_t *len)
{
	uint32_t rnd = mwc32();
	char path[PATH_MAX];
	char *mnts[MAX_MOUNTS];
	int i, n, fd = -1;

	*len = 0;
	n = mount_get(mnts, SIZEOF_ARRAY(mnts));
	if (n < 0)
		return -1;

	(void)umask(0077);

	for (i = 0; i < n; i++) {
		struct statfs buf;

		/* Some paths should be avoided... */
		if (!strncmp(mnts[i], "/dev", 4))
			continue;
		if (!strncmp(mnts[i], "/sys", 4))
			continue;
		if (!strncmp(mnts[i], "/run/lock", 9))
			continue;
		if (statfs(mnts[i], &buf) < 0)
			continue;

		/* ..and must be TMPFS too.. */
		if (buf.f_type != TMPFS_MAGIC)
			continue;

		/* We have a candidate, try to create a tmpfs file */
		(void)snprintf(path, sizeof(path), "%s/%s-%d-%" PRIu32 "-%" PRIu32,
			mnts[i], args->name, args->pid, args->instance, rnd);
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd >= 0) {
			int rc;
			const char data = 0;
			off_t max_size = buf.f_bsize * buf.f_bavail;

			/*
			 * Don't use all the tmpfs, just 98% for all instance
			 */
			max_size = (max_size * 98) / 100;
			max_size /= stressor_instances(STRESS_TMPFS);

			(void)unlink(path);
			/*
			 *  make file with hole; we want this
			 *  to be autopopulated with pages
			 *  over time
			 */
			rc = lseek(fd, max_size, SEEK_SET);
			if (rc < 0) {
				(void)close(fd);
				continue;
			}
			rc = write(fd, &data, sizeof(data));
			if (rc < 0) {
				(void)close(fd);
				continue;
			}
			*len = max_size;
			break;
		}
	}
	mount_free(mnts, n);

	return fd;
}

static void stress_tmpfs_child(
	const args_t *args,
	const int fd,
	int *flags,
	const size_t page_size,
	const size_t sz,
	const size_t pages4k)
{
	int no_mem_retries = 0;
	const int ms_flags = (g_opt_flags & OPT_FLAGS_MMAP_ASYNC) ?
		MS_ASYNC : MS_SYNC;

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;
		const int rnd = mwc32() % SIZEOF_ARRAY(mmap_flags);
		const int rnd_flag = mmap_flags[rnd];
		uint8_t *buf = NULL;

		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (!g_keep_stressing_flag)
			break;
		buf = (uint8_t *)mmap(NULL, sz,
			PROT_READ | PROT_WRITE, *flags | rnd_flag, fd, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			*flags &= ~MAP_POPULATE;
#endif
			no_mem_retries++;
			if (no_mem_retries > 1)
				(void)shim_usleep(100000);
			continue;	/* Try again */
		}
		if (g_opt_flags & OPT_FLAGS_MMAP_FILE) {
			memset(buf, 0xff, sz);
			(void)shim_msync((void *)buf, sz, ms_flags);
		}
		(void)madvise_random(buf, sz);
		(void)mincore_touch_pages(buf, sz);
		memset(mapped, PAGE_MAPPED, sizeof(mapped));
		for (n = 0; n < pages4k; n++)
			mappings[n] = buf + (n * page_size);

		/* Ensure we can write to the mapped pages */
		mmap_set(buf, sz, page_size);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (mmap_check(buf, sz, page_size) < 0)
				pr_fail("%s: mmap'd region of %zu bytes does "
					"not contain expected data\n", args->name, sz);
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)mincore_touch_pages(buf, sz);
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc64() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					(void)madvise_random(mappings[page], page_size);
					(void)munmap((void *)mappings[page], page_size);
					n--;
					break;
				}
				if (!g_keep_stressing_flag)
					goto cleanup;
			}
		}
		(void)munmap((void *)buf, sz);
#if defined(MAP_FIXED)
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc64() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (!mapped[page]) {
					off_t offset = (g_opt_flags & OPT_FLAGS_MMAP_FILE) ?
							page * page_size : 0;
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page] = (uint8_t *)mmap((void *)mappings[page],
						page_size, PROT_READ | PROT_WRITE, MAP_FIXED | *flags, fd, offset);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)mincore_touch_pages(mappings[page], page_size);
						(void)madvise_random(mappings[page], page_size);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						mmap_set(mappings[page], page_size, page_size);
						if (mmap_check(mappings[page], page_size, page_size) < 0)
							pr_fail("%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", args->name, page_size);
						if (g_opt_flags & OPT_FLAGS_MMAP_FILE) {
							memset(mappings[page], n, page_size);
							(void)shim_msync((void *)mappings[page], page_size, ms_flags);
						}
					}
					n--;
					break;
				}
				if (!g_keep_stressing_flag)
					goto cleanup;
			}
		}
#endif
cleanup:
		/*
		 *  Step #3, unmap them all
		 */
		for (n = 0; n < pages4k; n++) {
			if (mapped[n] & PAGE_MAPPED) {
				(void)madvise_random(mappings[n], page_size);
				(void)munmap((void *)mappings[n], page_size);
			}
		}
		inc_counter(args);
	} while (keep_stressing());
}

/*
 *  stress_tmpfs()
 *	stress tmpfs
 */
int stress_tmpfs(const args_t *args)
{
	const size_t page_size = args->page_size;
	off_t sz;
	size_t pages4k;
	pid_t pid;
	int fd, flags = MAP_SHARED;
	uint32_t ooms = 0, segvs = 0, buserrs = 0;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	fd = stress_tmpfs_open(args, &sz);
	if (fd < 0) {
		pr_err("%s: cannot find writeable free space on a "
			"tmpfs filesystem\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	pages4k = (size_t)sz / page_size;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(args->name, true);

again:
	if (!g_keep_stressing_flag)
		goto cleanup;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
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
			/* If we got killed by sigbus, re-start */
			if (WTERMSIG(status) == SIGBUS) {
				/* Happens frequently, so be silent */
				buserrs++;
				goto again;
			}

			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				ooms++;
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				segvs++;
				goto again;
			}
		}
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		stress_tmpfs_child(args, fd, &flags, page_size, sz, pages4k);
	}

cleanup:
	(void)close(fd);
	if (ooms + segvs + buserrs > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", SIGBUS signals: %" PRIu32 "\n",
			args->name, ooms, segvs, buserrs);

	return EXIT_SUCCESS;
}
#else
int stress_tmpfs(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
