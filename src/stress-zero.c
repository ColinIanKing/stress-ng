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
	{ NULL,	"zero N",	"start N workers reading /dev/zero" },
	{ NULL,	"zero-ops N",	"stop after N /dev/zero bogo read operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_is_not_zero()
 *	checks if buffer is zero, buffer must be 64 bit aligned
 */
static bool stress_is_not_zero(uint64_t *buffer, const size_t len)
{
	register const uint8_t *end8 = ((uint8_t *)buffer) + len;
	register uint8_t *ptr8;
	register const uint64_t *end64 = buffer + (len / sizeof(uint64_t));
	register uint64_t *ptr64;

	for (ptr64 = buffer; ptr64 < end64; ptr64++) {
		if (*ptr64)
			return true;
	}
	for (ptr8 = (uint8_t *)ptr64; ptr8 < end8; ptr8++) {
		if (*ptr8)
			return true;
	}
	return false;
}

#if defined(__linux__)

typedef struct {
	const int flag;
	const char *flag_str;
} mmap_flags_t;

#define MMAP_FLAG_INFO(x)	{ x, # x }

/*
 *  A subset of mmap flags to exercise on /dev/zero mmap'ings
 */
static const mmap_flags_t mmap_flags[] = {
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS),
#if defined(MAP_LOCKED)
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS | MAP_LOCKED),
#endif
#if defined(MAP_POPULATE)
	MMAP_FLAG_INFO(MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE),
	MMAP_FLAG_INFO(MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE),
#endif
};
#endif

/*
 *  stress_zero
 *	stress reading of /dev/zero
 */
static int stress_zero(const stress_args_t *args)
{
	int fd;
	const size_t page_size = args->page_size;
	void *rd_buffer, *wr_buffer;
#if defined(__minix__)
	const int flags = O_RDONLY;
#else
	const int flags = O_RDWR;
#endif

	rd_buffer = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (rd_buffer == MAP_FAILED) {
		pr_fail("%s: cannot allocate page sized read buffer, skipping test\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	wr_buffer = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (wr_buffer == MAP_FAILED) {
		pr_fail("%s: cannot allocate page sized write buffer, skipping test\n",
			args->name);
		(void)munmap(rd_buffer, page_size);
		return EXIT_NO_RESOURCE;
	}

	if ((fd = open("/dev/zero", flags)) < 0) {
		pr_fail("%s: open /dev/zero failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
#if defined(__linux__)
		int32_t *ptr;
		size_t i;
#endif

		ret = read(fd, rd_buffer, page_size);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (stress_is_not_zero((uint64_t *)rd_buffer, (size_t)ret)) {
			pr_fail("%s: non-zero value from a read of /dev/zero\n",
				args->name);
		}

#if !defined(__minix__)
		/* One can also write to /dev/zero w/o failure */
		ret = write(fd, wr_buffer, page_size);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
#endif

#if defined(__linux__)
		for (i = 0; i < SIZEOF_ARRAY(mmap_flags); i++) {
			/*
			 *  check if we can mmap /dev/zero
			 */
			ptr = mmap(NULL, page_size, PROT_READ, mmap_flags[i].flag,
				fd, page_size * stress_mwc16());
			if (ptr == MAP_FAILED) {
				if ((errno == ENOMEM) || (errno == EAGAIN))
					continue;
				pr_fail("%s: mmap /dev/zero using %s failed, errno=%d (%s)\n",
					args->name, mmap_flags[i].flag_str, errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			if (stress_is_not_zero((uint64_t *)rd_buffer, (size_t)ret)) {
				pr_fail("%s: memory mapped page of /dev/zero using %s is not zero\n",
					args->name, mmap_flags[i].flag_str);
			}
			(void)munmap(ptr, page_size);
		}
#endif


		/*
		 *  lseek on /dev/zero just because we can
		 */
		(void)lseek(fd, SEEK_SET, 0);
		(void)lseek(fd, SEEK_END, 0);
		(void)lseek(fd, SEEK_CUR, 0);

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);

	(void)munmap(wr_buffer, page_size);
	(void)munmap(rd_buffer, page_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_zero_info = {
	.stressor = stress_zero,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.help = help
};
