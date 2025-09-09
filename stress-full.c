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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-helper.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-target-clones.h"
#include "core-pragma.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"full N",	"start N workers exercising /dev/full" },
	{ NULL, "full-ops N",	"stop after N /dev/full bogo I/O operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)	||	\
    defined(__sun__) ||		\
    defined(__FreeBSD__) ||	\
    defined(__NetBSD__)

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
#endif

/*
 *  stress_full
 *	stress /dev/full
 */
static int stress_full(stress_args_t *args)
{
	void *buffer;
	const size_t buffer_size = 4096;
#if defined(__linux__)
	size_t w = 0;
#endif
	int rc = EXIT_FAILURE;
	int fd = -1;

	buffer = stress_mmap_populate(NULL, buffer_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, buffer_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffer, buffer_size, "io-buffer");
	(void)stress_madvise_mergeable(buffer, buffer_size);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
		uint8_t *ptr;
		struct stat statbuf;

		if ((fd = open("/dev/full", O_RDWR)) < 0) {
			if (errno == ENOENT) {
				if (stress_instance_zero(args))
					pr_inf_skip("%s: /dev/full not available, skipping stressor\n",
						args->name);
				rc = EXIT_NOT_IMPLEMENTED;
				goto fail;
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
			const off_t offset = (sizeof(offset) == sizeof(uint64_t)) ?
				(off_t)(stress_mwc64() & 0x7fffffffffffffff) :
				(off_t)(stress_mwc32() & 0x7fffffffUL);
			ret = pread(fd, buffer, buffer_size, offset);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: read failed at offset %" PRIdMAX ", errno=%d (%s)\n",
					args->name, (intmax_t)offset, errno, strerror(errno));
				goto fail;
			}
		}
#endif
		/*
		 *  Try fstat
		 */
		ret = shim_fstat(fd, &statbuf);
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

#if defined(__linux__)
		{
			/*
			 *  On Linux seeks will always succeed
			 */
			const off_t offset = (off_t)stress_mwc64();

			ret = lseek(fd, offset, whences[w].whence);
			if (ret < 0) {
				pr_fail("%s: lseek(fd, %" PRIdMAX ", %s) failed, errno=%d (%s)\n",
					args->name, (intmax_t)offset, whences[w].name,
					errno, strerror(errno));
				goto fail;
			}
		}
		w++;
		if (w >= SIZEOF_ARRAY(whences))
			w = 0;
#endif

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

const stressor_info_t stress_full_info = {
	.stressor = stress_full,
	.classifier = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_full_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
