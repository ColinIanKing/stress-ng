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
	{ NULL,	"verity N",		"start N workers exercising file verity ioctls" },
	{ NULL,	"verity-ops N",		"stop after N file verity bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LINUX_FSVERITY_H) &&		\
    defined(HAVE_FSVERITY_ENABLE_ARG) &&	\
    defined(HAVE_FSVERITY_DIGEST) &&		\
    defined(FS_VERITY_HASH_ALG_SHA256) &&	\
    defined(FS_IOC_ENABLE_VERITY) &&		\
    defined(FS_IOC_MEASURE_VERITY)

/*
 *  stress_verity
 *	stress file verity
 */
static int stress_verity(const stress_args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;
	uint32_t rnd = stress_mwc32();

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args, filename, sizeof(filename), rnd);
	do {
		struct fsverity_enable_arg enable;
		char digest_buf[256];
		struct fsverity_digest *digest = (struct fsverity_digest *)digest_buf;
		char block[512];
		int i;

		fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			pr_err("%s: cannot create %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			return exit_status(errno);
		}
		for (i = 0; i < 16; i++) {
			const off_t off = (off_t)i * 64 * 1024;

			(void)memset(block, i, sizeof(block));
			ret = (int)lseek(fd, off, SEEK_SET);
			(void)ret;

			ret = (int)write(fd, block, sizeof(block));
			if (ret < 0) {
				ret = exit_status(errno);
				pr_err("%s: cannot write %s\n", args->name, filename);
				goto clean;
			}
		}
		(void)shim_fsync(fd);
		(void)close(fd);
		(void)sync();

		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			ret = exit_status(errno);
			pr_err("%s: cannot re-open %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto clean;
		}

		(void)memset(&enable, 0, sizeof(enable));
		enable.version = 1;
		enable.hash_algorithm = FS_VERITY_HASH_ALG_SHA256;
		enable.block_size = args->page_size;
		enable.salt_size = 0;
		enable.salt_ptr = (intptr_t)NULL;
		enable.sig_size = 0;
		enable.sig_ptr = (intptr_t)NULL;

		ret = ioctl(fd, FS_IOC_ENABLE_VERITY, &enable);
		if (ret < 0) {
			switch (errno) {
			case ENOTTY:
			case EOPNOTSUPP:
				pr_inf("%s: verity is not supported on the "
					"file system or by the kernel, skipping stress test\n",
					args->name);
				ret = EXIT_NOT_IMPLEMENTED;
				break;
			case ENOPKG:
				pr_inf("%s: kernel does not have sha256 "
					"crypto enabled\n",
					args->name);
				ret = EXIT_NOT_IMPLEMENTED;
				break;
			case EROFS:
			case EACCES:
			case EBUSY:
			case EINTR:
				ret = EXIT_NO_RESOURCE;
				break;
			default:
				pr_inf("%s: verity ioctl FS_IOC_ENABLE_VERITY "
					"failed on file %s, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				ret = EXIT_FAILURE;
			}
			(void)close(fd);
			goto clean;
		}

		/*
		 *  Exercise measuring verity, ignore return for now
		 */
		digest->digest_algorithm = FS_VERITY_HASH_ALG_SHA256;
		digest->digest_size = 32;
		ret = ioctl(fd, FS_IOC_MEASURE_VERITY, digest);
		(void)ret;

#if defined(FS_IOC_GETFLAGS) &&	\
    defined(FS_VERITY_FL)
		{
			int flags = 0;

			ret = ioctl(fd, FS_IOC_GETFLAGS, &flags);
			if (!(flags & FS_VERITY_FL)) {
				pr_fail("%s: verity enabled but FS_VERITY_FL bit not "
					"set on file flags from ioctl FS_IOC_GETFLAGS\n",
					args->name);
			}
		}
#endif
		(void)close(fd);

		/*
		 *  Read data back, should exercise verity verification
		 */
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			ret = exit_status(errno);
			pr_err("%s: cannot re-open %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto clean;
		}
		for (i = 0; i < 16; i++) {
			const off_t off = (off_t)i * 64 * 1024;

			(void)memset(block, i, sizeof(block));
			ret = (int)lseek(fd, off, SEEK_SET);
			(void)ret;

			ret = (int)read(fd, block, sizeof(block));
			if (ret < 0) {
				ret = exit_status(errno);
				pr_err("%s: cannot read %s\n", args->name, filename);
				goto clean;
			}
			if (block[0] != i) {
				pr_err("%s: data in file block %d is incorrect\n",
					args->name, i);
				goto clean;
			}
		}
		(void)shim_fsync(fd);

		(void)close(fd);
		(void)unlink(filename);

		inc_counter(args);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;

clean:
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

stressor_info_t stress_verity_info = {
	.stressor = stress_verity,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_verity_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
