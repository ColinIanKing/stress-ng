/*
 * Copyright (C) 2016 Canonical, Ltd.
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

#if defined(STRESS_FIEMAP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <linux/fs.h>
#include <linux/fiemap.h>

#define MAX_FIEMAP_PROCS	(4)		/* Number of FIEMAP stressors */

static uint64_t opt_fiemap_size = DEFAULT_FIEMAP_SIZE;
static bool set_fiemap_size = false;

void stress_set_fiemap_size(const char *optarg)
{
	set_fiemap_size = true;
	opt_fiemap_size = get_uint64_byte(optarg);
	check_range("fiemap-size", opt_fiemap_size,
		MIN_FIEMAP_SIZE, MAX_FIEMAP_SIZE);
}

/*
 *  stress_fiemap_writer()
 *	write data in random places and punch holes
 *	in data in random places to try and maximize
 *	extents in the file
 */
int stress_fiemap_writer(
	const char *name,
	const int fd,
	uint64_t *counters,
	const uint64_t max_ops)
{
	uint8_t buf[1];
	uint64_t len = (off_t)opt_fiemap_size - sizeof(buf);
	uint64_t counter;
	int rc = EXIT_FAILURE;
#if defined(FALLOC_FL_PUNCH_HOLE) && \
    defined(FALLOC_FL_KEEP_SIZE)
	bool punch_hole = true;
#endif

	stress_strnrnd((char *)buf, sizeof(buf));

	do {
		uint64_t offset;
		size_t i;	
		counter = 0;

		offset = (mwc64() % len) & ~0x1fff;
		if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
			break;
		if (write(fd, buf, sizeof(buf)) < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_fail_err(name, "write");
				goto tidy;
			}
		}
#if defined(FALLOC_FL_PUNCH_HOLE) && \
    defined(FALLOC_FL_KEEP_SIZE)
		if (!punch_hole)
			continue;

		offset = mwc64() % len;
		if (fallocate(fd, FALLOC_FL_PUNCH_HOLE |
				  FALLOC_FL_KEEP_SIZE, offset, 8192) < 0) {
			if (errno == EOPNOTSUPP)
				punch_hole = false;
		}
#endif
		for (i = 0; i < MAX_FIEMAP_PROCS; i++)
			counter += counters[i];
	} while (opt_do_run && (!max_ops || counter < max_ops));
	rc = EXIT_SUCCESS;
tidy:
	(void)close(fd);

	return rc;
}

/*
 *  stress_fiemap_ioctl()
 *	exercise the FIEMAP ioctl
 */
void stress_fiemap_ioctl(
	const char *name,
	int fd,
	uint64_t *const counter,
	const uint64_t max_ops)
{
	do {
		struct fiemap *fiemap, *tmp;
		size_t extents_size;

		fiemap = (struct fiemap *)calloc(1, sizeof(struct fiemap));
		if (!fiemap) {
			pr_err(stderr, "Out of memory allocating fiemap\n");
			break;
		}
		fiemap->fm_length = ~0;

		/* Find out how many extents there are */
		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			pr_fail_err(name, "FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}

		/* Read in the extents */
		extents_size = sizeof(struct fiemap_extent) *
			(fiemap->fm_mapped_extents);

		/* Resize fiemap to allow us to read in the extents */
		tmp = (struct fiemap *)realloc(fiemap, sizeof(struct fiemap) + extents_size);
		if (!tmp) {
			pr_fail_err(name, "FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}
		fiemap = tmp;

		memset(fiemap->fm_extents, 0, extents_size);
		fiemap->fm_extent_count = fiemap->fm_mapped_extents;
		fiemap->fm_mapped_extents = 0;

		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			pr_fail_err(name, "FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}
		free(fiemap);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  stress_fiemap_spawn()
 *	helper to spawn off fiemap stressor
 */
static inline pid_t stress_fiemap_spawn(
	const char *name,
	const int fd,
	uint64_t *const counter,
	const uint64_t max_ops)
{
        pid_t pid;

        pid = fork();
        if (pid < 0)
                return -1;
        if (pid == 0) {
                setpgid(0, pgrp);
                stress_parent_died_alarm();
		stress_fiemap_ioctl(name, fd, counter, max_ops);
                exit(EXIT_SUCCESS);
        }
        setpgid(pid, pgrp);
        return pid;
}


/*
 *  stress_fiemap
 *	stress fiemap IOCTL
 */
int stress_fiemap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_FIEMAP_PROCS], mypid;
	int ret, fd, rc = EXIT_FAILURE, status;
	char filename[PATH_MAX];
	size_t i;
	const size_t counters_sz = sizeof(uint64_t) * MAX_FIEMAP_PROCS;
	uint64_t *counters;
	uint64_t ops_per_proc = max_ops / MAX_FIEMAP_PROCS;
	uint64_t ops_remaining = max_ops % MAX_FIEMAP_PROCS;

	if (!set_fiemap_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fiemap_size = MAX_SEEK_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fiemap_size = MIN_SEEK_SIZE;
	}

	/* We need some share memory for counter accounting */
	counters = mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err(stderr, "%s: mmap failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	memset(counters, 0, counters_sz);

	mypid = getpid();
	ret = stress_temp_dir_mk(name, mypid, instance);
	if (ret < 0) {
		rc = exit_status(-ret);
		goto clean;
	}

	(void)stress_temp_filename(filename, sizeof(filename),
		name, mypid, instance, mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto clean;
	}
	(void)unlink(filename);

	for (i = 0; i < MAX_FIEMAP_PROCS; i++) {
		uint64_t ops = ops_per_proc +
			((i == 0) ? ops_remaining : 0);
		pids[i] = stress_fiemap_spawn(name, fd,
				&counters[i], ops);
		if (pids[i] < 0)
			goto fail;
	}
	rc = stress_fiemap_writer(name, fd, counters, max_ops);

	/* And reap stressors */
	for (i = 0; i < MAX_FIEMAP_PROCS; i++) {
		(void)kill(pids[i], SIGKILL);
		(void)waitpid(pids[i], &status, 0);
		(*counter) += counters[i];
	}
fail:
	(void)close(fd);
clean:
	(void)munmap(counters, counters_sz);
	(void)stress_temp_dir_rm(name, mypid, instance);
	return rc;
}

#endif
