/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-killpid.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FIEMAP_H)
#include <linux/fiemap.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#define MIN_FIEMAP_SIZE		(1 * MB)
#define MAX_FIEMAP_SIZE		(MAX_FILE_LIMIT)
#define DEFAULT_FIEMAP_SIZE	(16 * MB)

#define MAX_FIEMAP_PROCS	(4)		/* Number of FIEMAP stressors */
#define COUNT_MAX		(128)

static const stress_help_t help[] = {
	{ NULL,	"fiemap N",	  "start N workers exercising the FIEMAP ioctl" },
	{ NULL,	"fiemap-bytes N", "specify size of file to fiemap" },
	{ NULL,	"fiemap-ops N",	  "stop after N FIEMAP ioctl bogo operations" },
	{ NULL,	NULL,		   NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_fiemap_bytes, "fiemap-bytes", TYPE_ID_UINT64_BYTES_FS, MIN_FIEMAP_SIZE, MAX_FIEMAP_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_LINUX_FS_H) &&		\
    defined(HAVE_LINUX_FIEMAP_H) && 	\
    defined(FS_IOC_FIEMAP)

static void *counter_lock;	/* Counter lock */

/*
 *  stress_fiemap_writer()
 *	write data in random places and punch holes
 *	in data in random places to try and maximize
 *	extents in the file
 */
static int stress_fiemap_writer(
	stress_args_t *args,
	const int fd,
	const uint64_t fiemap_bytes)
{
	uint8_t buf[1];
	const uint64_t len = fiemap_bytes - sizeof(buf);
	int rc = EXIT_FAILURE;
#if defined(FALLOC_FL_PUNCH_HOLE) && \
    defined(FALLOC_FL_KEEP_SIZE)
	bool punch_hole = true;
#endif

	*buf = stress_mwc8();

	do {
		uint64_t offset;

		offset = stress_mwc64modn(len) & ~0x1fffUL;
		if (UNLIKELY(lseek(fd, (off_t)offset, SEEK_SET) < 0))
			break;
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;
		if (UNLIKELY(write(fd, buf, sizeof(buf)) < 0)) {
			if (errno == ENOSPC)
				continue;
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto tidy;
			}
		}
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;
#if defined(FALLOC_FL_PUNCH_HOLE) && \
    defined(FALLOC_FL_KEEP_SIZE)
		if (!punch_hole)
			continue;
		(void)shim_usleep(1000);

		offset = stress_mwc64modn(len);
		if (UNLIKELY(shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE |
					    FALLOC_FL_KEEP_SIZE, (off_t)offset, 8192) < 0)) {
			if (errno == ENOSPC)
				continue;
			if (errno == EOPNOTSUPP)
				punch_hole = false;
		}
		(void)shim_usleep(1000);
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;
#else
		UNEXPECTED
#endif
	} while (stress_bogo_inc_lock(args, counter_lock, false));
	rc = EXIT_SUCCESS;
tidy:
	(void)close(fd);

	return rc;
}

/*
 *  stress_fiemap_ioctl()
 *	exercise the FIEMAP ioctl
 */
static void stress_fiemap_ioctl(
	stress_args_t *args,
	const int fd)
{
#if !defined(O_SYNC)
	int c = stress_mwc32modn(COUNT_MAX);
#endif

	do {
		struct fiemap *fiemap, *tmp;
		size_t extents_size;

		fiemap = (struct fiemap *)calloc(1, sizeof(*fiemap));
		if (UNLIKELY(!fiemap)) {
			pr_err("%s: out of memory allocating fiemap%s\n",
				args->name, stress_get_memfree_str());
			break;
		}
		fiemap->fm_length = ~0UL;

		/* Find out how many extents there are */
		if (UNLIKELY(ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0)) {
			if (errno == EOPNOTSUPP) {
				if (stress_instance_zero(args))
					pr_inf_skip("%s: ioctl FS_IOC_FIEMAP not supported on the file system, skipping stressor\n",
						args->name);
				free(fiemap);
				break;
			}
			pr_fail("%s: ioctl FS_IOC_FIEMAP failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			free(fiemap);
			break;
		}
		(void)shim_sched_yield();
		if (UNLIKELY(!stress_continue(args))) {
			free(fiemap);
			break;
		}

		/* Read in the extents */
		extents_size = sizeof(struct fiemap_extent) *
			(fiemap->fm_mapped_extents);

		/* Resize fiemap to allow us to read in the extents */
		tmp = (struct fiemap *)realloc(fiemap,
			sizeof(*fiemap) + extents_size);
		if (UNLIKELY(!tmp)) {
			pr_fail("%s: realloc failed%s, errno=%d (%s)\n",
				args->name, stress_get_memfree_str(),
				errno, strerror(errno));
			free(fiemap);
			break;
		}
		fiemap = tmp;

		(void)shim_memset(fiemap->fm_extents, 0, extents_size);
		fiemap->fm_extent_count = fiemap->fm_mapped_extents;
		fiemap->fm_mapped_extents = 0;

		if (UNLIKELY(ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0)) {
			pr_fail("%s: ioctl FS_IOC_FIEMAP failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			free(fiemap);
			break;
		}
		free(fiemap);
		(void)shim_sched_yield();
		if (UNLIKELY(!stress_continue(args)))
			break;
#if !defined(O_SYNC)
		if (UNLIKELY(c++ > COUNT_MAX)) {
			c = 0;
			fdatasync(fd);
		}
#endif
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}

/*
 *  stress_fiemap_spawn()
 *	helper to spawn off fiemap stressor
 */
static inline pid_t stress_fiemap_spawn(
	stress_args_t *args,
	const int fd,
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid)
{
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		return -1;
	} else if (s_pid->pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		s_pid->pid = getpid();

		stress_sync_start_wait_s_pid(s_pid);

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		stress_mwc_reseed();
		stress_fiemap_ioctl(args, fd);
		_exit(EXIT_SUCCESS);
	} else {
		stress_sync_start_s_pid_list_add(s_pids_head, s_pid);
	}
	return s_pid->pid;
}

/*
 *  stress_fiemap
 *	stress fiemap IOCTL
 */
static int stress_fiemap(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	int ret, fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	size_t n;
	uint64_t fiemap_bytes, fiemap_bytes_total = DEFAULT_FIEMAP_SIZE;
	struct fiemap fiemap;
	const char *fs_type;
#if defined(O_SYNC)
	const int flags = O_CREAT | O_RDWR | O_SYNC;
#else
	const int flags = O_CREAT | O_RDWR;
#endif

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_err("%s: failed to create counter lock\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	s_pids = stress_sync_s_pids_mmap(MAX_FIEMAP_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs, skipping stressor\n", args->name, MAX_FIEMAP_PROCS);
		stress_lock_destroy(counter_lock);
		return EXIT_NO_RESOURCE;
	}

	if (!stress_get_setting("fiemap-bytes", &fiemap_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fiemap_bytes_total = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fiemap_bytes_total = MIN_FIEMAP_SIZE;
	}
	if (fiemap_bytes_total < MIN_FIEMAP_SIZE) {
		fiemap_bytes_total = MIN_FIEMAP_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --fiemap-bytes too small, using %" PRIu64 " instead\n",
				args->name, fiemap_bytes_total);
	}
	if (fiemap_bytes_total > MAX_FIEMAP_SIZE) {
		fiemap_bytes_total = MAX_FIEMAP_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --fiemap-bytes too large, using %" PRIu64 " instead\n",
				args->name, fiemap_bytes_total);
	}

	fiemap_bytes = fiemap_bytes_total / args->instances;
	if (fiemap_bytes < MIN_FIEMAP_SIZE) {
		fiemap_bytes = MIN_FIEMAP_SIZE;
		fiemap_bytes_total = fiemap_bytes * args->instance;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, fiemap_bytes, fiemap_bytes_total);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto clean;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto dir_clean;
	}
	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	(void)shim_memset(&fiemap, 0, sizeof(fiemap));
	fiemap.fm_length = ~0UL;
	if (ioctl(fd, FS_IOC_FIEMAP, &fiemap) < 0) {
		errno = EOPNOTSUPP;
		if (errno == EOPNOTSUPP) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: ioctl FS_IOC_FIEMAP not supported on the file system, skipping stressor%s\n",
					args->name, fs_type);
			rc = EXIT_NOT_IMPLEMENTED;
			goto close_clean;
		}
	}

	for (n = 0; n < MAX_FIEMAP_PROCS; n++) {
		stress_sync_start_init(&s_pids[n]);

		if (UNLIKELY(!stress_continue(args))) {
			rc = EXIT_SUCCESS;
			goto reap;
		}

		if (stress_fiemap_spawn(args, fd, &s_pids_head, &s_pids[n]) < 0)
			goto reap;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
        stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_fiemap_writer(args, fd, fiemap_bytes);
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* And reap stressors */
	stress_kill_and_wait_many(args, s_pids, n, SIGALRM, true);
close_clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
dir_clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_sync_s_pids_munmap(s_pids, MAX_FIEMAP_PROCS);
	stress_lock_destroy(counter_lock);

	return rc;
}

const stressor_info_t stress_fiemap_info = {
	.stressor = stress_fiemap,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_fiemap_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/fiemap.h, linux/fs.h or ioctl() FS_IOC_FIEMAP support"
};
#endif
