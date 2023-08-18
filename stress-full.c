// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-put.h"
#include "core-pragma.h"

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"full N",	"start N workers exercising /dev/full" },
	{ NULL, "full-ops N",	"stop after N /dev/full bogo I/O operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

typedef struct {
	const char *name;
	const int whence;
} stress_whences_t;

static const stress_whences_t whences[] = {
	{ "SEEK_SET",	SEEK_SET },
	{ "SEEK_CUR",	SEEK_CUR },
	{ "SEEK_END",	SEEK_END }
};

/*
 *  stress_data_is_not_zero()
 *	checks if buffer is zero, buffer expected to be page aligned
 */
static bool OPTIMIZE3 stress_data_is_not_zero(void *buffer, const size_t len)
{
	register const uint64_t *end64 = (uint64_t *)((uintptr_t)buffer + (len / sizeof(uint64_t)));
	register uint64_t *ptr64;

PRAGMA_UNROLL_N(8)
	for (ptr64 = buffer; ptr64 < end64; ptr64++) {
		if (UNLIKELY(*ptr64))
			return true;
	}
	return false;
}

/*
 *  stress_full
 *	stress /dev/full
 */
static int stress_full(const stress_args_t *args)
{
	void *buffer;
	const size_t buffer_size = 4096;
	size_t w = 0;
	int rc = EXIT_FAILURE;
	int fd = -1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	buffer = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zd bytes, errno=%d (%s), skipping stressor\n",
			args->name, buffer_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	do {
		ssize_t ret;
		off_t offset;
		uint8_t *ptr;
		struct stat statbuf;

		if ((fd = open("/dev/full", O_RDWR)) < 0) {
			if (errno == ENOENT) {
				if (args->instance == 0)
					pr_inf_skip("%s: /dev/full not available, skipping stress test\n",
						args->name);
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail("%s: open /dev/full failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto fail;
		}

		/*
		 *  Writes should always return -ENOSPC
		 */
		ret = write(fd, buffer, buffer_size);
		if (UNLIKELY(ret != -1)) {
			pr_fail("%s: write to /dev/null should fail "
				"with errno ENOSPC but it didn't\n",
				args->name);
			goto fail;
		}
		if ((errno == EAGAIN) || (errno == EINTR))
			goto try_read;
		if (errno != ENOSPC) {
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto fail;
		}

try_read:
		/*
		 *  Reads should always work
		 */
		ret = read(fd, buffer, buffer_size);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto fail;
		}
		if (stress_data_is_not_zero(buffer, buffer_size)) {
			pr_fail("%s: buffer does not contain all zeros\n", args->name);
			goto fail;
		}
#if defined(HAVE_PREAD)
		{
			offset = (sizeof(offset) == sizeof(uint64_t)) ?
				(off_t)(stress_mwc64() & 0x7fffffffffffffff) :
				(off_t)(stress_mwc32() & 0x7fffffffUL);
			ret = pread(fd, buffer, buffer_size, offset);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: read failed at offset %jd, errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				goto fail;
			}
		}
#endif
		/*
		 *  Try fstat
		 */
		ret = fstat(fd, &statbuf);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: fstat failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto fail;
		}
		/*
		 *  Try mmap'ing and msync on fd
		 */
		ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_READ,
			MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_uint8_put(*ptr);
#if defined(MS_SYNC)
			(void)msync((void *)ptr, args->page_size, MS_SYNC);
#else
			UNEXPECTED
#endif
			(void)munmap((void *)ptr, args->page_size);
		}
		ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_WRITE,
			MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED) {
			*ptr = 0;
			(void)munmap((void *)ptr, args->page_size);
		}

		/*
		 *  Seeks will always succeed
		 */
		offset = (off_t)stress_mwc64();
		ret = lseek(fd, offset, whences[w].whence);
		if (ret < 0) {
			pr_fail("%s: lseek(fd, %jd, %s)\n",
				args->name, (intmax_t)offset, whences[w].name);
			goto fail;
		}
		w++;
		if (w >= SIZEOF_ARRAY(whences))
			w = 0;

		/*
		 *  Exercise a couple of ioctls
		 */
#if defined(FIONREAD)
		{
			int isz = 0;

			/* Should be inappropriate ioctl */
			VOID_RET(int, ioctl(fd, FIONREAD, &isz));
		}
#endif
#if defined(FIGETBSZ)
		{
			int isz = 0;

			VOID_RET(int, ioctl(fd, FIGETBSZ, &isz));
		}
#endif
		(void)close(fd);
		fd = -1;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;

fail:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd >= 0)
		(void)close(fd);
	(void)munmap((void *)buffer, buffer_size);

	return rc;
}

stressor_info_t stress_full_info = {
	.stressor = stress_full,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_full_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
