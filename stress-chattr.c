/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"chattr N",	"start N workers thrashing chattr file mode bits " },
	{ NULL,	"chattr-ops N",	"stop chattr workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

#define EXT2_SECRM_FL			0x00000001 /* Secure deletion */
#define EXT2_UNRM_FL			0x00000002 /* Undelete */
#define EXT2_COMPR_FL			0x00000004 /* Compress file */
#define EXT2_SYNC_FL			0x00000008 /* Synchronous updates */
#define EXT2_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define EXT2_APPEND_FL			0x00000020 /* Writes to file may only append */
#define EXT2_NODUMP_FL			0x00000040 /* Do not dump file */
#define EXT2_NOATIME_FL			0x00000080 /* Do not update atime */
#define EXT3_JOURNAL_DATA_FL		0x00004000 /* File data should be journaled */
#define EXT2_NOTAIL_FL			0x00008000 /* File tail should not be merged */
#define EXT2_DIRSYNC_FL			0x00010000 /* Synchronous directory modifications */
#define EXT2_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define EXT4_EXTENTS_FL			0x00080000 /* Inode uses extents */
#define FS_NOCOW_FL			0x00800000 /* Do not cow file */
#define EXT4_PROJINHERIT_FL		0x20000000 /* Create with parents projid */

#define EXT2_IOC_GETFLAGS		_IOR('f', 1, long)
#define EXT2_IOC_SETFLAGS		_IOW('f', 2, long)

static const unsigned long flags[] = {
	EXT2_NOATIME_FL,	/* chattr 'A' */
	EXT2_SYNC_FL, 		/* chattr 'S' */
	EXT2_DIRSYNC_FL,	/* chattr 'D' */
	EXT2_APPEND_FL,		/* chattr 'a' */
	EXT2_COMPR_FL,		/* chattr 'c' */
	EXT2_NODUMP_FL,		/* chattr 'd' */
	EXT4_EXTENTS_FL, 	/* chattr 'e' */
	EXT2_IMMUTABLE_FL, 	/* chattr 'i' */
	EXT3_JOURNAL_DATA_FL, 	/* chattr 'j' */
	EXT4_PROJINHERIT_FL, 	/* chattr 'P' */
	EXT2_SECRM_FL, 		/* chattr 's' */
	EXT2_UNRM_FL, 		/* chattr 'u' */
	EXT2_NOTAIL_FL, 	/* chattr 't' */
	EXT2_TOPDIR_FL, 	/* chattr 'T' */
	FS_NOCOW_FL, 		/* chattr 'C' */
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

	for (i = 0; (i < 128) && keep_stressing(); i++) {
		int fd, fdw, ret;
		unsigned long zero = 0UL;
		ssize_t n;

		fd = open(filename, O_RDONLY | O_NONBLOCK | O_CREAT, S_IRUSR | S_IWUSR);
		if (fd < 0)
			continue;

		ret = ioctl(fd, EXT2_IOC_SETFLAGS, &zero);
		if (ret < 0) {
			pr_inf("%s: ioctl failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}

		fdw = open(filename, O_RDWR);
		if (fdw < 0)
			goto tidy;
		n = write(fdw, &zero, sizeof(zero));
		(void)n;

		ret = ioctl(fd, EXT2_IOC_SETFLAGS, &flag);
		if ((ret < 0) && (errno == EOPNOTSUPP))
			rc = -1;
		(void)ret;

		n = write(fdw, &zero, sizeof(zero));
		(void)n;

		ret = ioctl(fd, EXT2_IOC_SETFLAGS, &zero);
		(void)ret;

		(void)close(fdw);
tidy:
		(void)close(fd);
		(void)unlink(filename);
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

	/*
	 *  Allow for multiple workers to chattr the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = exit_status(errno);
			pr_fail_err("mkdir");
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	do {
		size_t i, fail = 0;

		for (i = 0; i < SIZEOF_ARRAY(flags); i++) {
			if (do_chattr(args, filename, flags[i]) < 0)
				fail++;
		}

		if (fail == i) {
			pr_inf("%s: chattr not supported on filesystem, skipping stressor\n",
				args->name);
			rc = EXIT_NOT_IMPLEMENTED;
			break;
		}
		inc_counter(args);
	} while (keep_stressing());

	(void)unlink(filename);
	(void)rmdir(pathname);

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
        .help = help
};

#endif
