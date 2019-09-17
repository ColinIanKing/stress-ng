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
} data_t;

static volatile bool keep_running;
static sigset_t set;
static shim_pthread_spinlock_t spinlock;

/*
 *  stress_inode_flags_ioctl()
 *	try and toggle an inode flag on/off
 */
static void stress_inode_flags_ioctl(
	const args_t *args,
	const int fd,
	const int flag)
{
	int ret, attr;

	if (!(keep_running || keep_stressing()))
		return;

	ret = ioctl(fd, FS_IOC_GETFLAGS, &attr);
	if (ret != 0)
		return;

	attr |= flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	(void)ret;

	attr &= ~flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	(void)ret;
}

/*
 *  stress_inode_flags_ioctl_sane()
 *	set flags to a sane state so that file can be removed
 */
static inline void stress_inode_flags_ioctl_sane(const int fd)
{
	int ret;
	const int flag = 0;

	ret = ioctl(fd, FS_IOC_SETFLAGS, &flag);
	(void)ret;
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
	const args_t *args,
	data_t *data)
{
	while (keep_running && keep_stressing()) {
		int ret;

		stress_inode_flags_ioctl(args, data->dir_fd, 0);
#if defined(FS_DIRSYNC_FL)
		stress_inode_flags_ioctl(args, data->dir_fd, FS_DIRSYNC_FL);
#endif
#if defined(FS_PROJINHERIT_FL)
		stress_inode_flags_ioctl(args, data->dir_fd, FS_PROJINHERIT_FL);
#endif
#if defined(FS_SYNC_FL)
		stress_inode_flags_ioctl(args, data->dir_fd, FS_SYNC_FL);
#endif
#if defined(FS_TOPDIR_FL)
		stress_inode_flags_ioctl(args, data->dir_fd, FS_TOPDIR_FL);
#endif
#if defined(FS_APPEND_LF)
		stress_inode_flags_ioctl(args, data->file_fd, FS_APPEND_FL);
#endif
#if defined(FS_COMPR_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_COMPR_FL);
#endif
#if defined(FS_IMMUTABLE_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_IMMUTABLE_FL);
#endif
#if defined(FS_JOURNAL_DATA_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_JOURNAL_DATA_FL);
#endif
#if defined(FS_NOCOW_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_NOCOW_FL);
#endif
#if defined(FS_NODUMP_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_NODUMP_FL);
#endif
#if defined(FS_NOTAIL_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_NOTAIL_FL);
#endif
#if defined(FS_PROJINHERIT_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_PROJINHERIT_FL);
#endif
#if defined(FS_SECRM_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_SECRM_FL);
#endif
#if defined(FS_SYNC_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_SYNC_FL);
#endif
#if defined(FS_UNRM_FL)
		stress_inode_flags_ioctl(args, data->file_fd, FS_UNRM_FL);
#endif
		ret = shim_pthread_spin_lock(&spinlock);
		if (!ret) {
			inc_counter(args);
			ret = shim_pthread_spin_unlock(&spinlock);
			(void)ret;
		}
		stress_inode_flags_ioctl_sane(data->file_fd);
	}
	stress_inode_flags_ioctl_sane(data->file_fd);

	return 0;
}

/*
 *  stress_inode_flags()
 *	exercise inode flags on a file
 */
static void *stress_inode_flags_thread(void *arg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	pthread_args_t *pa = (pthread_args_t *)arg;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	pa->pthread_ret = stress_inode_flags_stressor(pa->args, pa->data);

	return &nowt;
}


/*
 *  stress_inode_flags
 *	stress reading all of /dev
 */
static int stress_inode_flags(const args_t *args)
{
	size_t i;
	pthread_t pthreads[MAX_INODE_FLAG_THREADS];
	int rc, ret[MAX_INODE_FLAG_THREADS];
	pthread_args_t pa[MAX_INODE_FLAG_THREADS];
	data_t data;
	char tmp[PATH_MAX], file_name[PATH_MAX];
	char *dir_name;

	rc = shim_pthread_spin_init(&spinlock, SHIM_PTHREAD_PROCESS_SHARED);
        if (rc) {
                pr_fail_errno("pthread_spin_init", rc);
                return EXIT_FAILURE;
        }

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return exit_status(-rc);
	(void)stress_temp_filename_args(args,
		file_name, sizeof(file_name), mwc32());

	shim_strlcpy(tmp, file_name, sizeof(tmp));
	dir_name = dirname(tmp);

	data.dir_fd = open(dir_name, O_RDONLY | O_DIRECTORY);
	if (data.dir_fd < 0) {
		pr_err("%s: cannot open %s: errno=%d (%s)\n",
			args->name, dir_name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}
	data.file_fd = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (data.file_fd < 0) {
		pr_err("%s: cannot open %s: errno=%d (%s)\n",
			args->name, file_name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_dir_fd;
	}

	(void)memset(ret, 0, sizeof(ret));
	keep_running = true;

	for (i = 0; i < MAX_INODE_FLAG_THREADS; i++) {
		pa[i].args = args;
		pa[i].data = (void *)&data;
		pa[i].pthread_ret = 0;

		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_inode_flags_thread, &pa[i]);
	}

	do {
		stress_inode_flags_stressor(args, &data);
	} while (keep_stressing());

	keep_running = false;
	rc = EXIT_SUCCESS;

	for (i = 0; i < MAX_INODE_FLAG_THREADS; i++) {
		if (ret[i] == 0) {
			pthread_join(pthreads[i], NULL);
			if (pa[i].pthread_ret < 0)
				rc = EXIT_FAILURE;
		}
	}

	(void)close(data.file_fd);
tidy_dir_fd:
	(void)close(data.dir_fd);
tidy:
	(void)unlink(file_name);
	stress_temp_dir_rm_args(args);
	(void)shim_pthread_spin_destroy(&spinlock);

	return rc;
}

stressor_info_t stress_inode_flags_info = {
	.stressor = stress_inode_flags,
	.class = CLASS_OS | CLASS_FILESYSTEM,
	.help = help
};
#else
stressor_info_t stress_inode_flags_info = {
	.stressor = stress_not_implemented ,
	.class = CLASS_OS | CLASS_FILESYSTEM,
	.help = help
};
#endif
