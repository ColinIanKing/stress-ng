/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"chattr N",	"start N workers thrashing chattr file mode bits " },
	{ NULL,	"chattr-ops N",	"stop chattr workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

#define SHIM_EXT2_SECRM_FL		0x00000001 /* Secure deletion */
#define SHIM_EXT2_UNRM_FL		0x00000002 /* Undelete */
#define SHIM_EXT2_COMPR_FL		0x00000004 /* Compress file */
#define SHIM_EXT2_SYNC_FL		0x00000008 /* Synchronous updates */
#define SHIM_EXT2_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define SHIM_EXT2_APPEND_FL		0x00000020 /* Writes to file may only append */
#define SHIM_EXT2_NODUMP_FL		0x00000040 /* Do not dump file */
#define SHIM_EXT2_NOATIME_FL		0x00000080 /* Do not update atime */
#define SHIM_EXT3_JOURNAL_DATA_FL	0x00004000 /* File data should be journaled */
#define SHIM_EXT2_NOTAIL_FL		0x00008000 /* File tail should not be merged */
#define SHIM_EXT2_DIRSYNC_FL		0x00010000 /* Synchronous directory modifications */
#define SHIM_EXT2_TOPDIR_FL		0x00020000 /* Top of directory hierarchies*/
#define SHIM_EXT4_EXTENTS_FL		0x00080000 /* Inode uses extents */
#define SHIM_FS_NOCOW_FL		0x00800000 /* Do not cow file */
#define SHIM_EXT4_PROJINHERIT_FL	0x20000000 /* Create with parents projid */

#define SHIM_EXT2_IOC_GETFLAGS		_IOR('f', 1, long)
#define SHIM_EXT2_IOC_SETFLAGS		_IOW('f', 2, long)

static const unsigned int flags[] = {
	SHIM_EXT2_NOATIME_FL,		/* chattr 'A' */
	SHIM_EXT2_SYNC_FL, 		/* chattr 'S' */
	SHIM_EXT2_DIRSYNC_FL,		/* chattr 'D' */
	SHIM_EXT2_APPEND_FL,		/* chattr 'a' */
	SHIM_EXT2_COMPR_FL,		/* chattr 'c' */
	SHIM_EXT2_NODUMP_FL,		/* chattr 'd' */
	SHIM_EXT4_EXTENTS_FL, 		/* chattr 'e' */
	SHIM_EXT2_IMMUTABLE_FL, 	/* chattr 'i' */
	SHIM_EXT3_JOURNAL_DATA_FL, 	/* chattr 'j' */
	SHIM_EXT4_PROJINHERIT_FL, 	/* chattr 'P' */
	SHIM_EXT2_SECRM_FL, 		/* chattr 's' */
	SHIM_EXT2_UNRM_FL, 		/* chattr 'u' */
	SHIM_EXT2_NOTAIL_FL, 		/* chattr 't' */
	SHIM_EXT2_TOPDIR_FL, 		/* chattr 'T' */
	SHIM_FS_NOCOW_FL, 		/* chattr 'C' */
};

/*
 *  do_chattr()
 */
static int do_chattr(
	const stress_args_t *args,
	const char *filename,
	const unsigned long flag)
{
	int i;
	int rc = 0;

	for (i = 0; (i < 128) && keep_stressing(args); i++) {
		int fd, fdw, ret;
		unsigned long zero = 0UL;
		unsigned long orig, rnd;

		fd = open(filename, O_RDONLY | O_NONBLOCK | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0)
			continue;

		if (!keep_stressing(args))
			goto tidy_fd;
		ret = ioctl(fd, SHIM_EXT2_IOC_GETFLAGS, &orig);
		if (ret < 0) {
			if ((errno != EOPNOTSUPP) &&
			    (errno != ENOTTY) &&
			    (errno != EINVAL))
				pr_inf("%s: ioctl SHIM_EXT2_IOC_GETFLAGS failed: errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_fs_type(filename));
			goto tidy_fd;
		}

		if (!keep_stressing(args))
			goto tidy_fd;
		ret = ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &zero);
		if (ret < 0) {
			rc = -1;
			if ((errno != EOPNOTSUPP) &&
			    (errno != ENOTTY) &&
			    (errno != EINVAL))
				pr_inf("%s: ioctl SHIM_EXT2_IOC_SETFLAGS failed: errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_fs_type(filename));
			goto tidy_fd;
		}

		if (!keep_stressing(args))
			goto tidy_fd;
		fdw = open(filename, O_RDWR);
		if (fdw < 0)
			goto tidy_fd;
		VOID_RET(ssize_t, write(fdw, &zero, sizeof(zero)));

		if (!keep_stressing(args))
			goto tidy_fdw;
		ret = ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &flag);
		if ((ret < 0) &&
                    ((errno == EOPNOTSUPP) ||
                     (errno == ENOTTY) ||
		     (errno == EINTR)))
			rc = -1;

		if (!keep_stressing(args))
			goto tidy_fdw;
		VOID_RET(ssize_t, write(fdw, &zero, sizeof(zero)));

		if (!keep_stressing(args))
			goto tidy_fdw;
		VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &zero));

		/*
		 *  Try some random flag, exercises any illegal flags
		 */
		if (!keep_stressing(args))
			goto tidy_fdw;
		rnd = 1ULL << (stress_mwc8() & 0x1f);
		VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &rnd));

		/*
		 *  Restore original setting
		 */
		if (!keep_stressing(args))
			goto tidy_fdw;
		VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &orig));
tidy_fdw:

		(void)close(fdw);
tidy_fd:
		(void)shim_fsync(fd);
		(void)close(fd);
		(void)shim_unlink(filename);
		return rc;
	}
	return rc;
}

/*
 *  stress_chattr
 *	stress chattr
 */
static int stress_chattr(const stress_args_t *args)
{
	const pid_t ppid = getppid();
	int rc = EXIT_SUCCESS;
	char filename[PATH_MAX], pathname[PATH_MAX];
	int all_flags, *flag_perms = NULL;
	size_t i, index, flag_count;

	for (all_flags = 0, i = 0; i < SIZEOF_ARRAY(flags); i++)
		all_flags |= flags[i];

	flag_count = stress_flag_permutation(all_flags, &flag_perms);

	/*
	 *  Allow for multiple workers to chattr the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = stress_exit_status(errno);
			pr_fail("%s: mkdir of %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			free(flag_perms);
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	index = 0;
	do {
		size_t fail = 0;

		for (i = 0; i < SIZEOF_ARRAY(flags); i++) {
			if (do_chattr(args, filename, (unsigned long)flags[i]) < 0)
				fail++;
		}

		if (fail == i) {
			if (args->instance == 0)
				pr_inf_skip("%s: chattr not supported on filesystem, skipping stressor\n",
					args->name);
			rc = EXIT_NOT_IMPLEMENTED;
			break;
		}

		/* Try next flag permutation */
		if ((flag_count > 0) && (flag_perms)) {
			(void)do_chattr(args, filename, (unsigned long)flag_perms[index]);
			index++;
			index %= flag_count;
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);
	free(flag_perms);

	return rc;
}

stressor_info_t stress_chattr_info = {
	.stressor = stress_chattr,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};

#else

stressor_info_t stress_chattr_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without Linux chattr() support"
};

#endif
