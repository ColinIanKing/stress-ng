/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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

#define MAX_FIEMAP_PROCS	(4)		/* Number of FIEMAP stressors */

static const help_t help[] = {
	{ NULL,	"fiemap N",	  "start N workers exercising the FIEMAP ioctl" },
	{ NULL,	"fiemap-ops N",	  "stop after N FIEMAP ioctl bogo operations" },
	{ NULL,	"fiemap-bytes N", "specify size of file to fiemap" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_fiemap_bytes(const char *opt)
{
	uint64_t fiemap_bytes;

	fiemap_bytes = get_uint64_byte_filesystem(opt, 1);
	check_range_bytes("fiemap-bytes", fiemap_bytes,
		MIN_FIEMAP_SIZE, MAX_FIEMAP_SIZE);
	return set_setting("fiemap-bytes", TYPE_ID_UINT64, &fiemap_bytes);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_fiemap_bytes,	stress_set_fiemap_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_LINUX_FS_H) &&		\
    defined(HAVE_LINUX_FIEMAP_H) && 	\
    defined(FS_IOC_FIEMAP)

/*
 *  stress_fiemap_writer()
 *	write data in random places and punch holes
 *	in data in random places to try and maximize
 *	extents in the file
 */
static int stress_fiemap_writer(
	const args_t *args,
	const int fd,
	const uint64_t fiemap_bytes,
	uint64_t *counters)
{
	uint8_t buf[1];
	const uint64_t len = (off_t)fiemap_bytes - sizeof(buf);
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
		if (!keep_stressing())
			break;
		if (write(fd, buf, sizeof(buf)) < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_fail_err("write");
				goto tidy;
			}
		}
		if (!keep_stressing())
			break;
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
		if (!keep_stressing())
			break;
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
	uint32_t c = 0;
	do {
		struct fiemap *fiemap, *tmp;
		size_t extents_size;

		/* Force periodic yields */
		c++;
		if (c >= 64) {
			c = 0;
			(void)shim_usleep(25000);
		}
		if (!keep_stressing())
			break;

		fiemap = (struct fiemap *)calloc(1, sizeof(*fiemap));
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
		if (!keep_stressing()) {
			free(fiemap);
			break;
		}

		/* Read in the extents */
		extents_size = sizeof(struct fiemap_extent) *
			(fiemap->fm_mapped_extents);

		/* Resize fiemap to allow us to read in the extents */
		tmp = (struct fiemap *)realloc(fiemap,
			sizeof(*fiemap) + extents_size);
		if (!tmp) {
			pr_fail_err("FS_IOC_FIEMAP ioctl()");
			free(fiemap);
			break;
		}
		fiemap = tmp;

		(void)memset(fiemap->fm_extents, 0, extents_size);
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
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_fiemap
 *	stress fiemap IOCTL
 */
static int stress_fiemap(const args_t *args)
{
	pid_t pids[MAX_FIEMAP_PROCS];
	int ret, fd, rc = EXIT_FAILURE, status;
	char filename[PATH_MAX];
	size_t i, n;
	const size_t counters_sz = sizeof(uint64_t) * MAX_FIEMAP_PROCS;
	uint64_t *counters;
	const uint64_t ops_per_proc = args->max_ops / MAX_FIEMAP_PROCS;
	const uint64_t ops_remaining = args->max_ops % MAX_FIEMAP_PROCS;
	uint64_t fiemap_bytes = DEFAULT_FIEMAP_SIZE;

	if (!get_setting("fiemap-bytes", &fiemap_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fiemap_bytes = MAX_SEEK_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fiemap_bytes = MIN_SEEK_SIZE;
	}
	fiemap_bytes /= args->num_instances;
	if (fiemap_bytes < MIN_FIEMAP_SIZE)
		fiemap_bytes = MIN_FIEMAP_SIZE;

	/* We need some share memory for counter accounting */
	counters = mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)memset(counters, 0, counters_sz);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = exit_status(-ret);
		goto clean;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto clean;
	}
	(void)unlink(filename);

	for (n = 0; n < MAX_FIEMAP_PROCS; n++) {
		uint64_t proc_max_ops = ops_per_proc +
			((n == 0) ? ops_remaining : 0);

		const args_t new_args = {
			.counter = &counters[n],
			.name = args->name,
			.max_ops = proc_max_ops,
			.instance = args->instance,
			.num_instances = args->num_instances,
			.pid = args->pid,
			.ppid = args->ppid,
			.page_size = args->page_size
		};

		if (!keep_stressing()) {
			rc = EXIT_SUCCESS;
			goto reap;
		}

		pids[n] = stress_fiemap_spawn(&new_args, fd);
		if (pids[n] < 0)
			goto reap;
	}
	rc = stress_fiemap_writer(args, fd, fiemap_bytes, counters);
reap:
	/* And reap stressors */
	for (i = 0; i < n; i++) {
		(void)kill(pids[i], SIGKILL);
		(void)shim_waitpid(pids[i], &status, 0);
		add_counter(args, counters[i]);
	}
	(void)close(fd);
clean:
	(void)munmap(counters, counters_sz);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

stressor_info_t stress_fiemap_info = {
	.stressor = stress_fiemap,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_fiemap_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
