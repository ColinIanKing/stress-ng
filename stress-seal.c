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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"seal N",	"start N workers performing fcntl SEAL commands" },
	{ NULL,	"seal-ops N",	"stop after N SEAL bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    defined(HAVE_MEMFD_CREATE)

#ifndef F_ADD_SEALS
#define F_ADD_SEALS		(1024 + 9)
#endif
#ifndef F_GET_SEALS
#define F_GET_SEALS		(1024 + 10)
#endif
#ifndef F_SEAL_SEAL
#define F_SEAL_SEAL		0x0001
#endif
#ifndef F_SEAL_SHRINK
#define F_SEAL_SHRINK		0x0002
#endif
#ifndef F_SEAL_GROW
#define F_SEAL_GROW		0x0004
#endif
#ifndef F_SEAL_WRITE
#define F_SEAL_WRITE		0x0008
#endif
#ifndef F_SEAL_FUTURE_WRITE
#define F_SEAL_FUTURE_WRITE	0x0010
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING	0x0002
#endif

/*
 *  stress_seal
 *	stress file sealing
 */
static int stress_seal(stress_args_t *args)
{
	int fd, ret;
	int rc = EXIT_FAILURE;
	const size_t page_size = args->page_size;
	char filename[PATH_MAX];
	char *buf;

	buf = stress_mmap_populate(NULL, page_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate %zu byte buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buf, page_size, "write-buffer");
	(void)shim_memset(buf, 0xff, page_size);
	(void)stress_madvise_mergeable(buf, page_size);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		const off_t sz = (off_t)page_size;
		uint8_t *ptr;
		ssize_t wret;

		(void)snprintf(filename, sizeof(filename), "%s-%" PRIdMAX "-%" PRIu32 "-%" PRIu32,
			args->name, (intmax_t)args->pid,
			args->instance, stress_mwc32());

		fd = shim_memfd_create(filename, MFD_ALLOW_SEALING);
		if (UNLIKELY(fd < 0)) {
			if (errno == ENOSYS) {
				pr_inf("%s: aborting, unimplemented "
					"system call memfd_created\n", args->name);
				(void)munmap((void *)buf, page_size);
				return EXIT_NO_RESOURCE;
			}
			pr_fail("%s: memfd_create %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			(void)munmap((void *)buf, page_size);
			return EXIT_FAILURE;
		}

		if (UNLIKELY(ftruncate(fd, sz) < 0)) {
			pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		if (UNLIKELY(fcntl(fd, F_GET_SEALS) < 0)) {
			pr_fail("%s: fcntl F_GET_SEALS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add shrink SEAL, file cannot be make smaller
		 */
		if (UNLIKELY(fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)) {
			pr_fail("%s: fcntl F_ADD_SEALS F_SEAL_SHRINK failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, 0);
		if (UNLIKELY((ret == 0) || ((ret < 0) && (errno != EPERM)))) {
			pr_fail("%s: ftruncate did not fail with EPERM as expected, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add grow SEAL, file cannot be made larger
		 */
		if (UNLIKELY(fcntl(fd, F_ADD_SEALS, F_SEAL_GROW) < 0)) {
			pr_fail("%s: fcntl F_ADD_SEALS F_SEAL_GROW failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, sz + 1);
		if (UNLIKELY((ret == 0) || ((ret < 0) && (errno != EPERM)))) {
			pr_fail("%s: ftruncate did not fail with EPERM as expected, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		/*
		 *  mmap file, sealing it will return EBUSY until
		 *  the mapping is removed
		 */
		ptr = mmap(NULL, (size_t)sz, PROT_WRITE, MAP_SHARED,
			fd, 0);
		if (UNLIKELY(ptr == MAP_FAILED)) {
			if (errno == ENOMEM)
				goto next;
			pr_fail("%s: mmap of %jd bytes failed%s, errno=%d (%s)\n",
				args->name, (intmax_t)sz,
				stress_get_memfree_str(), errno, strerror(errno));
			(void)close(fd);
			goto err;
		}
		(void)shim_memset(ptr, 0xea, page_size);
		ret = fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE);
		if (UNLIKELY((ret == 0) || ((ret < 0) && (errno != EBUSY)))) {
			pr_fail("%s: fcntl F_ADD_SEALS F_SEAL_WRITE did not fail with EBUSY as expected, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)stress_munmap_force(ptr, (size_t)sz);
			(void)close(fd);
			goto err;
		}
		(void)shim_msync(ptr, page_size, MS_SYNC);
		(void)stress_munmap_force(ptr, (size_t)sz);

		/*
		 *  Now write seal the file, no more writes allowed
		 */
		if (UNLIKELY(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0)) {
			if (errno == EBUSY)
				goto next;
			pr_fail("%s: fcntl F_ADD_SEALS F_SEAL_WRITE failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}
		wret = write(fd, buf, page_size);
		if (UNLIKELY((wret == 0) || ((wret < 0) && (errno != EPERM)))) {
			pr_fail("%s: write on sealed file did not fail with EPERM as expected, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		/*
		 *  And try (and ignore error) from a F_SEAL_FUTURE_WRITE
		 */
		VOID_RET(int, fcntl(fd, F_ADD_SEALS, F_SEAL_FUTURE_WRITE));
next:
		(void)close(fd);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
err:
	(void)munmap((void *)buf, page_size);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_seal_info = {
	.stressor = stress_seal,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_seal_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux memfd_create() system call support"
};
#endif
