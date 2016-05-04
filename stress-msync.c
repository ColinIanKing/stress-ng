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

#if defined(STRESS_MSYNC)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static size_t opt_msync_bytes = DEFAULT_MSYNC_BYTES;
static bool set_msync_bytes = false;
static sigjmp_buf jmp_env;
static uint64_t sigbus_count;

void stress_set_msync_bytes(const char *optarg)
{
	set_msync_bytes = true;
	opt_msync_bytes = (size_t)get_uint64_byte(optarg);
	check_range("mmap-bytes", opt_msync_bytes,
		MIN_MSYNC_BYTES, MAX_MSYNC_BYTES);
}

/*
 *  stress_page_check()
 *	check if mmap'd data is sane
 */
static int stress_page_check(
	uint8_t *buf,
	const uint8_t val,
	const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++) {
		if (buf[i] != val)
			return -1;
	}
	return 0;
}

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
 *  stress_msync()
 *	stress msync
 */
int stress_msync(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	const size_t page_size = stress_get_pagesize();
	const size_t min_size = 2 * page_size;
	size_t sz = min_size;
	ssize_t ret, rc = EXIT_SUCCESS;

	const pid_t pid = getpid();
	int fd = -1;
	char filename[PATH_MAX];

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		pr_fail_err(name, "sigsetjmp");
		return EXIT_FAILURE;
	}
	if (stress_sighandler(name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (!set_msync_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_msync_bytes = MAX_MSYNC_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_msync_bytes = MIN_MSYNC_BYTES;
	}
	sz = opt_msync_bytes & ~(page_size - 1);
	if (sz < min_size)
		sz = min_size;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	rc = stress_temp_dir_mk(name, pid, instance);
	if (rc < 0)
		return exit_status(-rc);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		(void)unlink(filename);
		(void)stress_temp_dir_rm(name, pid, instance);

		return rc;
	}
	(void)unlink(filename);

	if (ftruncate(fd, sz) < 0) {
		pr_err(stderr, "%s: ftruncate failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm(name, pid, instance);

		return EXIT_FAILURE;
	}

	buf = (uint8_t *)mmap(NULL, sz,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		pr_err(stderr, "%s: failed to mmap memory, errno=%d (%s)\n",
			name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto err;
	}

	do {
		off_t offset;
		uint8_t val, data[page_size];

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			/* Try again */
			continue;
		}
		/*
		 *  Change data in memory, msync to disk
		 */
		offset = (mwc64() % (sz - page_size)) & ~(page_size - 1);
		val = mwc8();

		memset(buf + offset, val, page_size);
		ret = msync(buf + offset, page_size, MS_SYNC);
		if (ret < 0) {
			pr_fail(stderr, "%s: msync MS_SYNC on "
				"offset %jd failed, errno=%d (%s)",
				name, (intmax_t)offset, errno, strerror(errno));
			goto do_invalidate;
		}
		ret = lseek(fd, offset, SEEK_SET);
		if (ret == (off_t)-1) {
			pr_err(stderr, "%s: cannot seet to offset %jd, "
				"errno=%d (%s)\n",
				name, (intmax_t)offset, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		ret = read(fd, data, sizeof(data));
		if (ret < (ssize_t)sizeof(data)) {
			pr_fail(stderr, "%s: read failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			goto do_invalidate;
		}
		if (stress_page_check(data, val, sizeof(data)) < 0) {
			pr_fail(stderr, "%s: msync'd data in file different "
				"to data in memory\n", name);
		}

do_invalidate:
		/*
		 *  Now change data on disc, msync invalidate
		 */
		offset = (mwc64() % (sz - page_size)) & ~(page_size - 1);
		val = mwc8();

		memset(buf + offset, val, page_size);

		ret = lseek(fd, offset, SEEK_SET);
		if (ret == (off_t)-1) {
			pr_err(stderr, "%s: cannot seet to offset %jd, errno=%d (%s)\n",
				name, (intmax_t)offset, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		ret = read(fd, data, sizeof(data));
		if (ret < (ssize_t)sizeof(data)) {
			pr_fail(stderr, "%s: read failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			goto do_next;
		}
		ret = msync(buf + offset, page_size, MS_INVALIDATE);
		if (ret < 0) {
			pr_fail(stderr, "%s: msync MS_INVALIDATE on "
				"offset %jd failed, errno=%d (%s)",
				name, (intmax_t)offset, errno, strerror(errno));
			goto do_next;
		}
		if (stress_page_check(buf + offset, val, sizeof(data)) < 0) {
			pr_fail(stderr, "%s: msync'd data in memory "
				"different to data in file\n", name);
		}
do_next:

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)munmap((void *)buf, sz);
err:
	(void)close(fd);
	(void)stress_temp_dir_rm(name, pid, instance);

	if (sigbus_count)
		pr_inf(stdout, "%s: caught %" PRIu64 " SIGBUS signals\n",
			name, sigbus_count);
	return rc;
}

#endif
