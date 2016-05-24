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

#if defined(STRESS_READAHEAD)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUF_ALIGNMENT		(4096)
#define BUF_SIZE		(512)
#define MAX_OFFSETS		(16)

static uint64_t opt_readahead_bytes = DEFAULT_READAHEAD_BYTES;
static bool set_readahead_bytes = false;

void stress_set_readahead_bytes(const char *optarg)
{
	set_readahead_bytes = true;
	opt_readahead_bytes =  get_uint64_byte(optarg);
	check_range("hdd-bytes", opt_readahead_bytes,
		MIN_HDD_BYTES, MAX_HDD_BYTES);
}

int do_readahead(
	const char *name,
	const int fd,
	off_t *offsets,
	const uint64_t readahead_bytes)
{
	int i;

	for (i = 0; i < MAX_OFFSETS; i++) {
		offsets[i] = (mwc64() % (readahead_bytes - BUF_SIZE)) & ~511;
		if (readahead(fd, offsets[i], BUF_SIZE) < 0) {
			pr_fail_err(name, "ftruncate");
			return -1;
		}
	}
	return 0;
}

/*
 *  stress_readahead
 *	stress file system cache via readahead calls
 */
int stress_readahead(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	uint64_t readahead_bytes, i;
	uint64_t misreads = 0;
	uint64_t baddata = 0;
	const pid_t pid = getpid();
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	int flags = O_CREAT | O_RDWR | O_TRUNC;
	int fd;
	struct stat statbuf;

	if (!set_readahead_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_readahead_bytes = MAX_HDD_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_readahead_bytes = MIN_HDD_BYTES;
	}

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	ret = posix_memalign((void **)&buf, BUF_ALIGNMENT, BUF_SIZE);
	if (ret || !buf) {
		rc = exit_status(errno);
		pr_err(stderr, "%s: cannot allocate buffer\n", name);
		(void)stress_temp_dir_rm(name, pid, instance);
		return rc;
	}

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	(void)umask(0077);
	if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto finish;
	}
	if (ftruncate(fd, (off_t)0) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "ftruncate");
		goto close_finish;
	}
	(void)unlink(filename);

#if defined(POSIX_FADV_DONTNEED)
	if (posix_fadvise(fd, 0, opt_readahead_bytes, POSIX_FADV_DONTNEED) < 0) {
		pr_fail_err(name, "posix_fadvise");
		goto close_finish;
	}
#endif

	/* Sequential Write */
	for (i = 0; i < opt_readahead_bytes; i += BUF_SIZE) {
		ssize_t ret;
		size_t j;
		off_t o = i / BUF_SIZE;
seq_wr_retry:
		if (!opt_do_run) {
			pr_inf(stderr, "%s: test expired during test setup "
				"(writing of data file)\n", name);
			rc = EXIT_SUCCESS;
			goto close_finish;
		}

		for (j = 0; j < BUF_SIZE; j++)
			buf[j] = (o + j) & 0xff;

		ret = pwrite(fd, buf, BUF_SIZE, i);
		if (ret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto seq_wr_retry;
			if (errno == ENOSPC)
				break;
			if (errno) {
				pr_fail_err(name, "pwrite");
				goto close_finish;
			}
			continue;
		}
	}

	if (fstat(fd, &statbuf) < 0) {
		pr_fail_err(name, "fstat");
		goto close_finish;
	}

	/* Round to write size to get no partial reads */
	readahead_bytes = (uint64_t)statbuf.st_size -
		(statbuf.st_size % BUF_SIZE);

	do {
		off_t offsets[MAX_OFFSETS];

		if (do_readahead(name, fd, offsets, readahead_bytes) < 0)
			goto close_finish;
				
		for (i = 0; i < MAX_OFFSETS; i++) {
rnd_rd_retry:
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
			ret = pread(fd, buf, BUF_SIZE, offsets[i]);
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto rnd_rd_retry;
				if (errno) {
					pr_fail_err(name, "read");
					goto close_finish;
				}
				continue;
			}
			if (ret != BUF_SIZE)
				misreads++;

			if (opt_flags & OPT_FLAGS_VERIFY) {
				size_t j;
				off_t o = offsets[i] / BUF_SIZE;

				for (j = 0; j < BUF_SIZE; j++) {
					uint8_t v = (o + j) & 0xff;
					if (buf[j] != v)
						baddata++;
				}
				if (baddata) {
					pr_fail(stderr, "error in data between %ju and %ju\n",
						(intmax_t)offsets[i],
						(intmax_t)offsets[i] + BUF_SIZE - 1);
				}
			}
			(*counter)++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
close_finish:
	(void)close(fd);
finish:
	free(buf);
	(void)stress_temp_dir_rm(name, pid, instance);

	if (misreads)
		pr_dbg(stderr, "%s: %" PRIu64 " incomplete random reads\n",
			name, misreads);

	return rc;
}

#endif
