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
	{ NULL,	"seal N",	"start N workers performing fcntl SEAL commands" },
	{ NULL,	"seal-ops N",	"stop after N SEAL bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) && defined(HAVE_MEMFD_CREATE)

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
static int stress_seal(const args_t *args)
{
	int fd, ret;
	int rc = EXIT_FAILURE;
	const size_t page_size = args->page_size;
	char filename[PATH_MAX];

	do {
		const off_t sz = page_size;
		uint8_t *ptr;
		char buf[page_size];

		(void)snprintf(filename, sizeof(filename), "%s-%d-%" PRIu32 "-%" PRIu32,
			args->name, args->pid, args->instance, mwc32());

		fd = shim_memfd_create(filename, MFD_ALLOW_SEALING);
		if (fd < 0) {
			if (errno == ENOSYS) {
				pr_inf("%s: aborting, unimplemented "
					"system call memfd_created\n", args->name);
				return EXIT_NO_RESOURCE;
			}
			pr_fail_err("memfd_create");
			return EXIT_FAILURE;
		}

		if (ftruncate(fd, sz) < 0) {
			pr_fail_err("ftruncate");
			(void)close(fd);
			goto err;
		}

		if (fcntl(fd, F_GET_SEALS) < 0) {
			pr_fail_err("fcntl F_GET_SEALS");
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add shrink SEAL, file cannot be make smaller
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
			pr_fail_err("fcntl F_ADD_SEALS F_SEAL_SHRINK");
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, 0);
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err("ftruncate did not fail with EPERM");
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add grow SEAL, file cannot be made larger
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_GROW) < 0) {
			pr_fail_err("fcntl F_ADD_SEALS F_SEAL_GROW");
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, sz + 1);
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err("ftruncate did not fail with EPERM");
			(void)close(fd);
			goto err;
		}

		/*
		 *  mmap file, sealing it will return EBUSY until
		 *  the mapping is removed
		 */
		ptr = mmap(NULL, sz, PROT_WRITE, MAP_SHARED,
			fd, 0);
		if (ptr == MAP_FAILED) {
			if (errno == ENOMEM)
				goto next;
			pr_fail_err("mmap");
			(void)close(fd);
			goto err;
		}
		(void)memset(ptr, 0xea, page_size);
		ret = fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE);
		if ((ret == 0) || ((ret < 0) && (errno != EBUSY))) {
			pr_fail_err("fcntl F_ADD_SEALS F_SEAL_WRITE did not fail with EBUSY");
			(void)munmap(ptr, sz);
			(void)close(fd);
			goto err;
		}
		(void)shim_msync(ptr, page_size, MS_SYNC);
		(void)munmap(ptr, sz);

		/*
		 *  Now write seal the file, no more writes allowed
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0) {
			pr_fail_err("fcntl F_ADD_SEALS F_SEAL_WRITE");
			(void)close(fd);
			goto err;
		}
		(void)memset(buf, 0xff, sizeof(buf));
		ret = write(fd, buf, sizeof(buf));
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err("write on sealed file did not fail with EPERM");
			(void)close(fd);
			goto err;
		}

		/*
		 *  And try (and ignore error) from a F_SEAL_FUTURE_WRITE
		 */
		ret = fcntl(fd, F_ADD_SEALS, F_SEAL_FUTURE_WRITE);
		(void)ret;
next:
		(void)close(fd);

		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
err:

	return rc;
}

stressor_info_t stress_seal_info = {
	.stressor = stress_seal,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_seal_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
