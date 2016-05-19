/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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

#include "stress-ng.h"

#if defined(STRESS_MADVISE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define NO_MEM_RETRIES_MAX	(256)

static sigjmp_buf jmp_env;
static uint64_t sigbus_count;

static const int madvise_options[] = {
#ifdef MADV_NORMAL
	MADV_NORMAL,
#endif
#ifdef MADV_RANDOM
	MADV_RANDOM,
#endif
#ifdef MADV_SEQUENTIAL
	MADV_SEQUENTIAL,
#endif
#ifdef MADV_WILLNEED
	MADV_WILLNEED,
#endif
#ifdef MADV_DONTNEED
	MADV_DONTNEED,
#endif
#ifdef MADV_REMOVE
	MADV_REMOVE,
#endif
#ifdef MADV_DONTFORK
	MADV_DONTFORK,
#endif
#ifdef MADV_DOFORK
	MADV_DOFORK,
#endif
#ifdef MADV_HWPOISON
	MADV_HWPOISON,
#endif
#ifdef MADV_MERGEABLE
	MADV_MERGEABLE,
#endif
#ifdef MADV_UNMERGEABLE
	MADV_UNMERGEABLE,
#endif
#ifdef MADV_SOFT_OFFLINE
	MADV_SOFT_OFFLINE,
#endif
#ifdef MADV_HUGEPAGE
	MADV_HUGEPAGE,
#endif
#ifdef MADV_NOHUGEPAGE
	MADV_NOHUGEPAGE,
#endif
#ifdef MADV_DONTDUMP
	MADV_DONTDUMP,
#endif
#ifdef MADV_DODUMP
	MADV_DODUMP,
#endif
#ifdef MADV_FREE
	MADV_FREE
#endif
};

/*
 *  stress_sigbus_handler()
 *     SIGBUS handler
 */
static void MLOCKED stress_sigbus_handler(int dummy)
{
	(void)dummy;

	sigbus_count++;

	siglongjmp(jmp_env, 1); /* bounce back */
}

/*
 *  stress_madvise()
 *	stress madvise
 */
int stress_madvise(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	const size_t page_size = stress_get_pagesize();
	size_t sz = 4 *  MB;
	const pid_t pid = getpid();
	int ret, fd = -1, flags = MAP_SHARED;
	int no_mem_retries = 0;
	char filename[PATH_MAX];
	char page[page_size];
	size_t n;

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail_err(name, "sigsetjmp");
		return EXIT_FAILURE;
	}
	if (stress_sighandler(name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

	sz &= ~(page_size - 1);

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	memset(page, 0xa5, page_size);

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err(name, "open");
		(void)unlink(filename);
		(void)stress_temp_dir_rm(name, pid, instance);
		return ret;
	}

	(void)unlink(filename);
	for (n = 0; n < sz; n += page_size) {
		ret = write(fd, page, sizeof(page));
		(void)ret;
	}

	do {
		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_err(stderr, "%s: gave up trying to mmap, no available memory\n",
				name);
			break;
		}

		if (!opt_do_run)
			break;
		buf = (uint8_t *)mmap(NULL, sz,
			PROT_READ | PROT_WRITE, flags, fd, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#ifdef MAP_POPULATE
			flags &= ~MAP_POPULATE;
#endif
			no_mem_retries++;
			if (no_mem_retries > 1)
				usleep(100000);
			continue;	/* Try again */
		}
		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			(void)munmap((void *)buf, sz);
			/* Try again */
			continue;
		}

		memset(buf, 0xff, sz);

		(void)madvise_random(buf, sz);
		(void)mincore_touch_pages(buf, sz);

		for (n = 0; n < sz; n += page_size) {
			const int idx = mwc32() % SIZEOF_ARRAY(madvise_options);
			const int advise = madvise_options[idx];

			(void)madvise(buf + n, page_size, advise);
			(void)msync(buf + n, page_size, MS_ASYNC);
		}
		for (n = 0; n < sz; n += page_size) {
			size_t m = (mwc64() % sz) & ~(page_size - 1);
			const int idx = mwc32() % SIZEOF_ARRAY(madvise_options);
			const int advise = madvise_options[idx];

			(void)madvise(buf + m, page_size, advise);
			(void)msync(buf + m, page_size, MS_ASYNC);
		}
		(void)munmap((void *)buf, sz);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	(void)stress_temp_dir_rm(name, pid, instance);

	if (sigbus_count)
		pr_inf(stdout, "%s: caught %" PRIu64 " SIGBUS signals\n",
			name, sigbus_count);
	return EXIT_SUCCESS;
}

#endif
