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

#if defined(STRESS_SEAL)

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>

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

#define MFD_ALLOW_SEALING	0x0002

static inline int sys_memfd_create(const char *name, unsigned int flags)
{
#if defined(__NR_memfd_create)
	return syscall(__NR_memfd_create, name, flags);
#else
	errno = ENOSYS;
	return -1;
#endif
}


/*
 *  stress_seal
 *	stress file sealing
 */
int stress_seal(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, ret;
	int rc = EXIT_FAILURE;
	const size_t page_size = stress_get_pagesize();
	const pid_t pid = getpid();
	char filename[PATH_MAX];

	do {
		const off_t sz = page_size;
		uint8_t *ptr;
		char buf[page_size];

		snprintf(filename, sizeof(filename), "%s-%d-%" PRIu32 "-%" PRIu32,
			name, pid, instance, mwc32());

		fd = sys_memfd_create(filename, MFD_ALLOW_SEALING);
		if (fd < 0) {
			if (errno == ENOSYS) {
				pr_inf(stderr, "%s: aborting, unimplemented "
					"system call memfd_created\n", name);
				return EXIT_NO_RESOURCE;
			}
			pr_fail_err(name, "memfd_create");
			return EXIT_FAILURE;
		}

		if (ftruncate(fd, sz) < 0) {
			pr_fail_err(name, "ftruncate");
			(void)close(fd);
			goto err;
		}

		if (fcntl(fd, F_GET_SEALS) < 0) {
			pr_fail_err(name, "fcntl F_GET_SEALS");
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add shrink SEAL, file cannot be make smaller
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
			pr_fail_err(name, "fcntl F_ADD_SEALS F_SEAL_SHRINK");
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, 0);
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err(name, "ftruncate did not fail with EPERM");
			(void)close(fd);
			goto err;
		}

		/*
		 *  Add grow SEAL, file cannot be made larger
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_GROW) < 0) {
			pr_fail_err(name, "fcntl F_ADD_SEALS F_SEAL_GROW");
			(void)close(fd);
			goto err;
		}
		ret = ftruncate(fd, sz + 1);
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err(name, "ftruncate did not fail with EPERM");
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
			pr_fail_err(name, "mmap");
			(void)close(fd);
			goto err;
		}
		memset(ptr, 0xea, page_size);
		ret = fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE);
		if ((ret == 0) || ((ret < 0) && (errno != EBUSY))) {
			pr_fail_err(name, "fcntl F_ADD_SEALS F_SEAL_WRITE did not fail with EBUSY");
			(void)munmap(ptr, sz);
			(void)close(fd);
			goto err;
		}
		(void)msync(ptr, page_size, MS_SYNC);
		(void)munmap(ptr, sz);

		/*
		 *  Now write seal the file, no more writes allowed
		 */
		if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0) {
			pr_fail_err(name, "fcntl F_ADD_SEALS F_SEAL_WRITE");
			(void)close(fd);
			goto err;
		}
		memset(buf, 0xff, sizeof(buf));
		ret = write(fd, buf, sizeof(buf));
		if ((ret == 0) || ((ret < 0) && (errno != EPERM))) {
			pr_fail_err(name, "write on sealed file did not fail with EPERM");
			(void)close(fd);
			goto err;
		}
next:
		(void)close(fd);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
err:

	return rc;
}

#endif
