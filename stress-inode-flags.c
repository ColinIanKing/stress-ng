/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-pthread.h"

#include <sys/ioctl.h>

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"inode-flags N",	"start N workers exercising various inode flags" },
	{ NULL,	"inode-flags-ops N",	"stop inode-flags workers after N bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_LIBGEN_H) && \
    defined(FS_IOC_GETFLAGS) && \
    defined(FS_IOC_SETFLAGS) && \
    defined(FS_IOC_SETFLAGS) && \
    defined(O_DIRECTORY)

#define MAX_INODE_FLAG_THREADS		(4)

typedef struct {
	int dir_fd;
	int file_fd;
} stress_data_t;

static void *inode_flags_counter_lock;
static volatile bool keep_running;
static sigset_t set;

static size_t inode_flag_count;
static int *inode_flag_perms;

static const int inode_flags[] = {
#if defined(FS_DIRSYNC_FL)
	FS_DIRSYNC_FL,
#endif
#if defined(FS_PROJINHERIT_FL)
	FS_PROJINHERIT_FL,
#endif
#if defined(FS_SYNC_FL)
	FS_SYNC_FL,
#endif
#if defined(FS_TOPDIR_FL)
	FS_TOPDIR_FL,
#endif
#if defined(FS_APPEND_FL)
	FS_APPEND_FL,
#endif
#if defined(FS_COMPR_FL)
	FS_COMPR_FL,
#endif
#if defined(FS_IMMUTABLE_FL)
	FS_IMMUTABLE_FL,
#endif
#if defined(FS_JOURNAL_DATA_FL)
	FS_JOURNAL_DATA_FL,
#endif
#if defined(FS_NOCOW_FL)
	FS_NOCOW_FL,
#endif
#if defined(FS_NODUMP_FL)
	FS_NODUMP_FL,
#endif
#if defined(FS_NOTAIL_FL)
	FS_NOTAIL_FL,
#endif
#if defined(FS_PROJINHERIT_FL)
	FS_PROJINHERIT_FL,
#endif
#if defined(FS_SECRM_FL)
	FS_SECRM_FL,
#endif
#if defined(FS_SYNC_FL)
	FS_SYNC_FL,
#endif
#if defined(FS_UNRM_FL)
	FS_UNRM_FL,
#endif
};

/*
 *  stress_inode_flags_ioctl()
 *	try and toggle an inode flag on/off
 */
static void stress_inode_flags_ioctl(
	stress_args_t *args,
	const int fd,
	const int flag)
{
	int ret, attr;

	if (!(keep_running || stress_continue(args)))
		return;

	ret = ioctl(fd, FS_IOC_GETFLAGS, &attr);
	if (ret != 0)
		return;

	if (flag)
		attr |= flag;
	else
		attr = 0;
	VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &attr));

	attr &= ~flag;
	VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &attr));
}

/*
 *  stress_inode_flags_ioctl_sane()
 *	set flags to a sane state so that file can be removed
 */
static inline void stress_inode_flags_ioctl_sane(const int fd)
{
	const long int flag = 0;

	VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &flag));
}

/*
 *  stress_inode_flags_stressor()
 *	exercise inode flags, see man ioctl_flags for
 *	more details of these flags. Some are never going
 *	to be implemented and some are just relevant to
 *	specific file systems. We just want to try and
 *	toggle these on and off to see if they break rather
 *	than fail.
 */
static int stress_inode_flags_stressor(
	stress_args_t *args,
	const stress_data_t *data)
{
	size_t idx = 0;

	while (keep_running && stress_continue(args)) {
		size_t i;

		/* Work through all inode flag permutations */
		stress_inode_flags_ioctl(args, data->dir_fd, inode_flag_perms[idx]);
		stress_inode_flags_ioctl(args, data->file_fd, inode_flag_perms[idx]);
		idx++;
		if (idx >= inode_flag_count)
			idx = 0;
		stress_inode_flags_ioctl_sane(data->dir_fd);
		stress_inode_flags_ioctl_sane(data->file_fd);

		stress_inode_flags_ioctl(args, data->dir_fd, 0);
		for (i = 0; stress_continue(args) && (i < SIZEOF_ARRAY(inode_flags)); i++)
			stress_inode_flags_ioctl(args, data->dir_fd, inode_flags[i]);
		stress_inode_flags_ioctl_sane(data->dir_fd);

		stress_inode_flags_ioctl(args, data->file_fd, 0);
		for (i = 0; stress_continue(args) && (i < SIZEOF_ARRAY(inode_flags)); i++)
			stress_inode_flags_ioctl(args, data->file_fd, inode_flags[i]);
		stress_inode_flags_ioctl_sane(data->file_fd);
		shim_fsync(data->file_fd);
		VOID_RET(bool, stress_bogo_inc_lock(args, inode_flags_counter_lock, 1));
	}

	return 0;
}

/*
 *  stress_inode_flags()
 *	exercise inode flags on a file
 */
static void *stress_inode_flags_thread(void *arg)
{
	static void *nowt = NULL;
	stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	pa->pthread_ret = stress_inode_flags_stressor(pa->args, pa->data);

	return &nowt;
}

/*
 *  stress_inode_flags
 *	stress reading all of /dev
 */
static int stress_inode_flags(stress_args_t *args)
{
	size_t i;
	pthread_t pthreads[MAX_INODE_FLAG_THREADS];
	int rc, ret[MAX_INODE_FLAG_THREADS], all_inode_flags;
	stress_pthread_args_t pa[MAX_INODE_FLAG_THREADS];
	stress_data_t data;
	char tmp[PATH_MAX], file_name[PATH_MAX];
	char *dir_name;

	inode_flags_counter_lock = stress_lock_create("counter");
	if (!inode_flags_counter_lock) {
		pr_inf("%s: failed to create lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	for (all_inode_flags = 0, i = 0; i < SIZEOF_ARRAY(inode_flags); i++)
		all_inode_flags |= inode_flags[i];

	inode_flag_count = stress_flag_permutation(all_inode_flags, &inode_flag_perms);

	if ((inode_flag_count == 0) || (!inode_flag_perms)) {
		pr_inf_skip("%s: no inode flags to exercise, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_lock;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		rc = stress_exit_status(-rc);
		goto tidy_lock;
	}
	(void)stress_temp_filename_args(args,
		file_name, sizeof(file_name), stress_mwc32());

	(void)shim_strscpy(tmp, file_name, sizeof(tmp));
	dir_name = dirname(tmp);

	data.dir_fd = open(dir_name, O_RDONLY | O_DIRECTORY);
	if (data.dir_fd < 0) {
		pr_err("%s: cannot open %s: errno=%d (%s)\n",
			args->name, dir_name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_unlink;
	}
	data.file_fd = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (data.file_fd < 0) {
		pr_err("%s: cannot open %s: errno=%d (%s)\n",
			args->name, file_name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_dir_fd;
	}

	(void)shim_memset(ret, 0, sizeof(ret));
	keep_running = true;

	for (i = 0; i < MAX_INODE_FLAG_THREADS; i++) {
		pa[i].args = args;
		pa[i].data = (void *)&data;
		pa[i].pthread_ret = 0;

		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_inode_flags_thread, &pa[i]);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_inode_flags_stressor(args, &data);
	} while (stress_continue(args));

	keep_running = false;
	rc = EXIT_SUCCESS;

	for (i = 0; i < MAX_INODE_FLAG_THREADS; i++) {
		if (ret[i] == 0) {
			(void)pthread_join(pthreads[i], NULL);
			if (pa[i].pthread_ret < 0)
				rc = EXIT_FAILURE;
		}
	}

	stress_inode_flags_ioctl_sane(data.dir_fd);
	stress_inode_flags_ioctl_sane(data.file_fd);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(data.file_fd);
tidy_dir_fd:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(data.dir_fd);
tidy_unlink:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_unlink(file_name);
	stress_temp_dir_rm_args(args);
tidy_lock:
	(void)stress_lock_destroy(inode_flags_counter_lock);

	return rc;
}

const stressor_info_t stress_inode_flags_info = {
	.stressor = stress_inode_flags,
	.class = CLASS_OS | CLASS_FILESYSTEM,
	.help = help
};
#else
const stressor_info_t stress_inode_flags_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_FILESYSTEM,
	.help = help,
	.unimplemented_reason = "built without libgen.h, linux/fs.h or pthread support"
};
#endif
