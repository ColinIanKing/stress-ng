// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

typedef struct {
	const unsigned long flag;
	const char attr;
} stress_chattr_flag_t;

static const stress_chattr_flag_t stress_chattr_flags[] = {
	{ SHIM_EXT2_NOATIME_FL,		'A' },
	{ SHIM_EXT2_SYNC_FL, 		'S' },
	{ SHIM_EXT2_DIRSYNC_FL,		'D' },
	{ SHIM_EXT2_APPEND_FL,		'a' },
	{ SHIM_EXT2_COMPR_FL,		'c' },
	{ SHIM_EXT2_NODUMP_FL,		'd' },
	{ SHIM_EXT4_EXTENTS_FL, 	'e' },
	{ SHIM_EXT2_IMMUTABLE_FL, 	'i' },
	{ SHIM_EXT3_JOURNAL_DATA_FL, 	'j' },
	{ SHIM_EXT4_PROJINHERIT_FL, 	'P' },
	{ SHIM_EXT2_SECRM_FL, 		's' },
	{ SHIM_EXT2_UNRM_FL, 		'u' },
	{ SHIM_EXT2_NOTAIL_FL, 		't' },
	{ SHIM_EXT2_TOPDIR_FL, 		'T' },
	{ SHIM_FS_NOCOW_FL, 		'C' },
};

static char *stress_chattr_flags_str(const unsigned long flags, char *str, const size_t str_len)
{
	unsigned int i;
	size_t j = 0;

	for (i = 0; i < SIZEOF_ARRAY(stress_chattr_flags); i++) {
		if (flags & stress_chattr_flags[i].flag) {
			if (j < str_len - 1) {
				str[j] = stress_chattr_flags[i].attr;
				j++;
			}
		}
	}
	str[j] = '\0';

	return str;
}

/*
 *  do_chattr()
 */
static int do_chattr(
	const stress_args_t *args,
	const char *filename,
	const unsigned long flags,
	const unsigned long mask)
{
	int i;
	int rc = EXIT_SUCCESS;

	for (i = 0; (i < 128) && stress_continue(args); i++) {
		int fd, fdw, ret;
		unsigned long zero = 0UL;
		unsigned long orig, tmp, check;
		bool verify;
		unsigned int j;

		fd = open(filename, O_RDWR | O_NONBLOCK | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0)
			continue;

		if (!stress_continue(args))
			goto tidy_fd;
		ret = ioctl(fd, SHIM_EXT2_IOC_GETFLAGS, &orig);
		if (ret < 0) {
			if ((errno != EOPNOTSUPP) &&
			    (errno != ENOTTY) &&
			    (errno != EINVAL)) {
				/* unexpected failure */
				pr_fail("%s: ioctl SHIM_EXT2_IOC_GETFLAGS failed: errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				rc = EXIT_FAILURE;
				goto tidy_fd;
			}
			/* most probably not supported */
			rc = EXIT_NO_RESOURCE;
			goto tidy_fd;
		}

		if (!stress_continue(args))
			goto tidy_fd;

		/* work through flags disabling them one by one */
		tmp = orig;
		for (j = 0; (j < sizeof(orig) * 8); j++) {
			register unsigned long bitmask = 1ULL << j;

			if (orig & bitmask) {
				tmp &= ~bitmask;

				ret = ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &tmp);
				if (ret < 0) {
					if ((errno != EOPNOTSUPP) &&
					    (errno != ENOTTY) &&
					    (errno != EPERM) &&
					    (errno != EINVAL)) {
						/* unexpected failure */
						pr_fail("%s: ioctl SHIM_EXT2_IOC_SETFLAGS (chattr zero flags) failed: errno=%d (%s)%s\n",
							args->name,
							errno, strerror(errno),
							stress_get_fs_type(filename));
						rc = EXIT_FAILURE;
						goto tidy_fd;
					}
					/* failed, so re-eable the bit */
					tmp |= bitmask;
				}
			}
		}

		if (!stress_continue(args))
			goto tidy_fd;
		fdw = open(filename, O_RDWR);
		if (fdw < 0)
			goto tidy_fd;
		VOID_RET(ssize_t, write(fdw, &zero, sizeof(zero)));

		if (!stress_continue(args))
			goto tidy_fdw;

		verify = (args->num_instances == 1);
		ret = ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &flags);
		if (ret < 0) {
			if ((errno != EOPNOTSUPP) &&
			    (errno != ENOTTY) &&
			    (errno != EPERM) &&
			    (errno != EINVAL)) {
				/* unexpected failure */
				char flags_str[65];

				stress_chattr_flags_str(flags, flags_str, sizeof(flags_str));
				pr_fail("%s: ioctl SHIM_EXT2_IOC_SETFLAGS 0x%lx (chattr '%s') failed: errno=%d (%s)%s\n",
					args->name, flags, flags_str,
					errno, strerror(errno),
					stress_get_fs_type(filename));
				rc = EXIT_FAILURE;
				goto tidy_fdw;
			}
			/* most probably not supported */
			rc = EXIT_NO_RESOURCE;
			goto tidy_fdw;
		}

		ret = ioctl(fd, SHIM_EXT2_IOC_GETFLAGS, &check);
		if (ret < 0) {
			if ((errno != EOPNOTSUPP) &&
			    (errno != ENOTTY) &&
			    (errno != EINVAL)) {
				/* unexpected failure */
				pr_fail("%s: ioctl SHIM_EXT2_IOC_GETFLAGS failed: errno=%d (%s)%s\n",
					args->name,
					errno, strerror(errno),
					stress_get_fs_type(filename));
				rc = EXIT_FAILURE;
				goto tidy_fdw;
			}
			/* most probably not supported */
			rc = EXIT_NO_RESOURCE;
			goto tidy_fdw;
		}

		/*
		 *  check that no other *extra* flag bits have been set
		 *  (as opposed to see if flags set is same as flags got
		 *  since some flag settings are not set if the filesystem
		 *  cannot honor them).
		 */
		if (verify) {
			tmp = mask & ~(SHIM_EXT3_JOURNAL_DATA_FL | SHIM_EXT4_EXTENTS_FL);
			if (((flags & tmp) | (check & tmp)) != (flags & tmp)) {
				char flags_str[65], check_str[65];

				stress_chattr_flags_str(flags & tmp, flags_str, sizeof(flags_str));
				stress_chattr_flags_str(check & tmp, check_str, sizeof(check_str));

				pr_fail("%s: EXT2_IOC_GETFLAGS returned different "
					"flags 0x%lx ('%s') from set flags 0x%lx ('%s')\n",
					args->name, check & tmp, check_str,
					flags & tmp, flags_str);

				rc = EXIT_FAILURE;
				goto tidy_fdw;
			}
		}

		if (!stress_continue(args))
			goto tidy_fdw;
		VOID_RET(ssize_t, write(fdw, &zero, sizeof(zero)));

		if (!stress_continue(args))
			goto tidy_fdw;
		VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &zero));

		/*
		 *  Try some random flag, exercises any illegal flags
		 */
		if (!stress_continue(args))
			goto tidy_fdw;
		tmp = 1ULL << (stress_mwc8() & 0x1f);
		VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &tmp));

		/*
		 *  Restore original setting
		 */
		tmp = 0;
		for (j = 0; (j < sizeof(orig) * 8); j++) {
			register unsigned long bitmask = 1ULL << j;

			tmp |= bitmask;
			ret = ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &tmp);
			if (ret < 0)
				tmp &= ~bitmask;
		}
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
	unsigned long mask = 0;;
	int *flag_perms = NULL;
	size_t i, index, flag_count;

	for (i = 0; i < SIZEOF_ARRAY(stress_chattr_flags); i++) {
		mask |= stress_chattr_flags[i].flag;
	}

	flag_count = stress_flag_permutation((int)mask, &flag_perms);

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

		for (i = 0; i < SIZEOF_ARRAY(stress_chattr_flags); i++) {
			int ret;

			ret = do_chattr(args, filename, stress_chattr_flags[i].flag, mask);
			switch (ret) {
			case EXIT_FAILURE:
				fail++;
				rc = ret;
				break;
			case EXIT_NO_RESOURCE:
				fail++;
				break;
			default:
				break;
			}
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
			(void)do_chattr(args, filename, (unsigned long)flag_perms[index], mask);
			index++;
			if (index >= flag_count)
				index = 0;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);
	free(flag_perms);

	return rc;
}

stressor_info_t stress_chattr_info = {
	.stressor = stress_chattr,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_chattr_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux chattr() support"
};

#endif
