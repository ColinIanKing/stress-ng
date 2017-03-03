/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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
#if defined(__linux__) && defined(FS_IOC_FIEMAP)
#include <linux/fs.h>
#include <linux/fiemap.h>
#endif

#define MAX_FIEMAP_PROCS	(4)		/* Number of FIEMAP stressors */

static uint64_t opt_fiemap_size = DEFAULT_FIEMAP_SIZE;
static bool set_fiemap_size = false;

void stress_set_fiemap_size(const char *optarg)
{
	set_fiemap_size = true;
	opt_fiemap_size =
		get_uint64_byte_filesystem(optarg,
			stressor_instances(STRESS_FIEMAP));
	check_range_bytes("fiemap-size", opt_fiemap_size,
		MIN_FIEMAP_SIZE, MAX_FIEMAP_SIZE);
}

#if defined(__linux__) && defined(FS_IOC_FIEMAP)

/*
 *  stress_fiemap_writer()
 *	write data in random places and punch holes
 *	in data in random places to try and maximize
 *	extents in the file
 */
static int stress_fiemap_writer(
	const args_t *args,
	const int fd,
	uint64_t *counters)
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
				pr_fail_err("write");
				goto tidy;
			}
		}
#if defined(FALLOC_FL_PUNCH_HOLE) && \
    defined(FALLOC_FL_KEEP_SIZE)
		if (!punch_hole)
			continue;

		offset = mwc64() % len;
		if (shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE |
				  FALLOC_FL_KEEP_SIZE, offset, 8192) < 0) {
			if (errno == EOPNOTSUPP)
				punch_hole = false;
		}
#endif
		for (i = 0; i < MAX_FIEMAP_PROCS; i++)
			counter += counters[i];
	} while (keep_stressing());
	rc = EXIT_SUCCESS;
tidy:
	(void)close(fd);

	return rc;
}

/*
 *  stress_fiemap_ioctl()
 *	exercise the FIEMAP ioctl
 */
static void stress_fiemap_ioctl(const args_t *args, int fd)
{
	do {
		struct fiemap *fiemap, *tmp;
		size_t extents_size;

		fiemap = (struct fiemap *)calloc(1, sizeof(struct fiemap));
		if (!fiemap) {
			pr_err("Out of memory allocating fiemap\n");
			break;
		}
		fiemap->fm_length = ~0;

		/* Find out how many extents there are */
		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			pr_fail_err("FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}

		/* Read in the extents */
		extents_size = sizeof(struct fiemap_extent) *
			(fiemap->fm_mapped_extents);

		/* Resize fiemap to allow us to read in the extents */
		tmp = (struct fiemap *)realloc(fiemap,
			sizeof(struct fiemap) + extents_size);
		if (!tmp) {
			pr_fail_err("FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}
		fiemap = tmp;

		memset(fiemap->fm_extents, 0, extents_size);
		fiemap->fm_extent_count = fiemap->fm_mapped_extents;
		fiemap->fm_mapped_extents = 0;

		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			pr_fail_err("FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}
		free(fiemap);
		inc_counter(args);
	} while (keep_stressing());
}

/*
 *  stress_fiemap_spawn()
 *	helper to spawn off fiemap stressor
 */
static inline pid_t stress_fiemap_spawn(
	const args_t *args,
	const int fd)
{
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		stress_fiemap_ioctl(args, fd);
		exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}


/*
 *  stress_fiemap
 *	stress fiemap IOCTL
 */
int stress_fiemap(const args_t *args)
{
	pid_t pids[MAX_FIEMAP_PROCS];
	int ret, fd, rc = EXIT_FAILURE, status;
	char filename[PATH_MAX];
	size_t i;
	const size_t counters_sz = sizeof(uint64_t) * MAX_FIEMAP_PROCS;
	uint64_t *counters;
	uint64_t ops_per_proc = args->max_ops / MAX_FIEMAP_PROCS;
	uint64_t ops_remaining = args->max_ops % MAX_FIEMAP_PROCS;

	if (!set_fiemap_size) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fiemap_size = MAX_SEEK_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fiemap_size = MIN_SEEK_SIZE;
	}

	/* We need some share memory for counter accounting */
	counters = mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	memset(counters, 0, counters_sz);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = exit_status(-ret);
		goto clean;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto clean;
	}
	(void)unlink(filename);

	for (i = 0; i < MAX_FIEMAP_PROCS; i++) {
		uint64_t proc_max_ops = ops_per_proc +
			((i == 0) ? ops_remaining : 0);

		const args_t new_args = {
			&counters[i],
			args->name,
			proc_max_ops,
			args->instance,
			args->pid,
			args->ppid,
			args->page_size
		};

		pids[i] = stress_fiemap_spawn(&new_args, fd);
		if (pids[i] < 0)
			goto fail;
	}
	rc = stress_fiemap_writer(args, fd, counters);

	/* And reap stressors */
	for (i = 0; i < MAX_FIEMAP_PROCS; i++) {
		(void)kill(pids[i], SIGKILL);
		(void)waitpid(pids[i], &status, 0);
		(*args->counter) += counters[i];
	}
fail:
	(void)close(fd);
clean:
	(void)munmap(counters, counters_sz);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}
#else
int stress_fiemap(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
