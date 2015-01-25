/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

static uint64_t opt_hdd_bytes = DEFAULT_HDD_BYTES;
static uint64_t opt_hdd_write_size = DEFAULT_HDD_WRITE_SIZE;

void stress_set_hdd_bytes(const char *optarg)
{
	opt_hdd_bytes =  get_uint64_byte(optarg);
	check_range("hdd-bytes", opt_hdd_bytes,
		MIN_HDD_BYTES, MAX_HDD_BYTES);
}

void stress_set_hdd_write_size(const char *optarg)
{
	opt_hdd_write_size = get_uint64_byte(optarg);
	check_range("hdd-write-size", opt_hdd_write_size,
		MIN_HDD_WRITE_SIZE, MAX_HDD_WRITE_SIZE);
}

#define BUF_ALIGNMENT	(4096)

/*
 *  stress_hdd
 *	stress I/O via writes
 */
int stress_hdd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	uint64_t i;
	const pid_t pid = getpid();
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	int flags =  O_CREAT | O_RDWR | O_TRUNC;

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	ret = posix_memalign((void **)&buf, BUF_ALIGNMENT, (size_t)opt_hdd_write_size);
	if (ret || !buf) {
		pr_err(stderr, "%s: cannot allocate buffer\n", name);
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}

	for (i = 0; i < opt_hdd_write_size; i++)
		buf[i] = (uint8_t)mwc();

#if defined(O_SYNC)
	if (opt_flags & OPT_FLAGS_HDD_SYNC)
		flags |= O_SYNC;
#endif
#if defined(O_DSYNC)
	if (opt_flags & OPT_FLAGS_HDD_DSYNC)
		flags |= O_DSYNC;
#endif
#if defined(O_DIRECT)
	if (opt_flags & OPT_FLAGS_HDD_DIRECT)
		flags |= O_DIRECT;
#endif
#if defined(O_NOATIME)
	if (opt_flags & OPT_FLAGS_HDD_NOATIME)
		flags |= O_NOATIME;
#endif

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());
	do {
		int fd;

		(void)umask(0077);
		if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
			pr_failed_err(name, "open");
			goto finish;
		}
		(void)unlink(filename);

		for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
			if (write(fd, buf, (size_t)opt_hdd_write_size) < 0) {
				pr_failed_err(name, "write");
				(void)close(fd);
				goto finish;
			}
			(*counter)++;
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
		}
		(void)close(fd);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
finish:
	free(buf);
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}
