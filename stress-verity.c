/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_FSVERITY_H)
#include <linux/fsverity.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"verity N",		"start N workers exercising file verity ioctls" },
	{ NULL,	"verity-ops N",		"stop after N file verity bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LINUX_FSVERITY_H) &&		\
    defined(HAVE_FSVERITY_ENABLE_ARG) &&	\
    defined(HAVE_FSVERITY_DIGEST) &&		\
    defined(FS_IOC_ENABLE_VERITY) &&		\
    defined(FS_IOC_MEASURE_VERITY) &&		\
    (defined(FS_VERITY_HASH_ALG_SHA256) ||	\
     defined(FS_VERITY_HASH_ALG_SHA512))

static const uint32_t hash_algorithms[] = {
#if defined(FS_VERITY_HASH_ALG_SHA256)
	FS_VERITY_HASH_ALG_SHA256,
#endif
#if defined(FS_VERITY_HASH_ALG_SHA512)
	FS_VERITY_HASH_ALG_SHA512,
#endif
};


/*
 *  For FS_IOC_READ_VERITY_METADATA, introduced in Linux 5.12
 */
struct shim_fsverity_read_metadata_arg {
	uint64_t metadata_type;
	uint64_t offset;
	uint64_t length;
	uint64_t buf_ptr;
	uint64_t reserved;
};

/*
 *  stress_verity
 *	stress file verity
 */
static int stress_verity(stress_args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;
	size_t hash = 0;

	if (SIZEOF_ARRAY(hash_algorithms) == (0)) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: no hash algorithms defined, skipping stressor\n",
				args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		struct fsverity_enable_arg enable;
		char digest_buf[256];
		struct fsverity_digest *digest = (struct fsverity_digest *)digest_buf;
		char block[512];
		int i;
#if defined(FS_IOC_READ_VERITY_METADATA)
		struct shim_fsverity_read_metadata_arg md_arg;
		char md_buf[4096];
#else
		UNEXPECTED
#endif

		fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			ret = stress_exit_status(errno);
			pr_err("%s: cannot create %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			return ret;
		}
		for (i = 0; i < 16; i++) {
			const off_t off = (off_t)i * 64 * 1024;
			ssize_t n;

			(void)shim_memset(block, i, sizeof(block));
			VOID_RET(off_t, lseek(fd, off, SEEK_SET));

			n = write(fd, block, sizeof(block));
			if (n < 0) {
				ret = stress_exit_status(errno);
				pr_err("%s: cannot write %s, errno=%d (%s)%s\n",
					args->name, filename,
					errno, strerror(errno),
					stress_get_fs_type(filename));
				(void)close(fd);
				goto clean;
			}
		}
		(void)shim_fsync(fd);
		(void)close(fd);
		shim_sync();

		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			ret = stress_exit_status(errno);
			pr_err("%s: cannot re-open %s, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno),
				stress_get_fs_type(filename));
			goto clean;
		}

		(void)shim_memset(&enable, 0, sizeof(enable));
		enable.version = 1;
		enable.hash_algorithm = hash_algorithms[hash];
		enable.block_size = (uint32_t)args->page_size;
		enable.salt_size = 0;
		enable.salt_ptr = (intptr_t)NULL;
		enable.sig_size = 0;
		enable.sig_ptr = (intptr_t)NULL;

		hash++;
		if (hash >= SIZEOF_ARRAY(hash_algorithms))
			hash = 0;

		ret = ioctl(fd, FS_IOC_ENABLE_VERITY, &enable);
		if (ret < 0) {
			switch (errno) {
			case EINVAL:
			case ENOTTY:
			case EOPNOTSUPP:
			case ENOSYS:
				if (stress_instance_zero(args))
					pr_inf_skip("%s: verity is not supported on the "
						"file system or by the kernel, skipping stressor\n",
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
			case ENOSPC:
				ret = EXIT_NO_RESOURCE;
				break;
			default:
				pr_inf("%s: verity ioctl FS_IOC_ENABLE_VERITY "
					"failed on file %s, errno=%d (%s)%s\n",
					args->name, filename, errno, strerror(errno),
					stress_get_fs_type(filename));
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
		VOID_RET(int, ioctl(fd, FS_IOC_MEASURE_VERITY, digest));

#if defined(FS_IOC_GETFLAGS) &&	\
    defined(FS_VERITY_FL)
		{
			int flags = 0;

			ret = ioctl(fd, FS_IOC_GETFLAGS, &flags);
			if ((ret == 0) && !(flags & FS_VERITY_FL)) {
				pr_fail("%s: verity enabled but FS_VERITY_FL bit not "
					"set on file flags from ioctl FS_IOC_GETFLAGS\n",
					args->name);
				(void)close(fd);
				ret = EXIT_FAILURE;
				goto clean;
			}
		}
#else
		UNEXPECTED
#endif
		(void)close(fd);

		/*
		 *  Read data back, should exercise verity verification
		 */
		fd = open(filename, O_RDONLY);
		if (fd < 0) {
			ret = stress_exit_status(errno);
			pr_err("%s: cannot re-open %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto clean;
		}
		for (i = 0; i < 16; i++) {
			const off_t off = (off_t)i * 64 * 1024;
			ssize_t n;

			(void)shim_memset(block, i, sizeof(block));
			VOID_RET(off_t, lseek(fd, off, SEEK_SET));

			n = read(fd, block, sizeof(block));
			if (n < 0) {
				ret = stress_exit_status(errno);
				pr_err("%s: cannot read %s, errno=%d (%s)%s\n",
					args->name, filename,
					errno, strerror(errno),
					stress_get_fs_type(filename));
				(void)close(fd);
				goto clean;
			}
			if (block[0] != i) {
				pr_fail("%s: data in file block %d is incorrect\n",
					args->name, i);
				(void)close(fd);
				ret = EXIT_FAILURE;
				goto clean;
			}
		}
		(void)shim_fsync(fd);

#if defined(FS_IOC_READ_VERITY_METADATA)
		(void)shim_memset(&md_arg, 0, sizeof(md_arg));
		md_arg.metadata_type = 0ULL;
		md_arg.offset = 0ULL;
		md_arg.buf_ptr = (uint64_t)(intptr_t)md_buf;
		md_arg.length = (uint64_t)sizeof(md_buf);

		VOID_RET(int, ioctl(fd, FS_IOC_READ_VERITY_METADATA, &md_arg));
#else
		UNEXPECTED
#endif

		(void)close(fd);
		(void)shim_unlink(filename);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;

clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

const stressor_info_t stress_verity_info = {
	.stressor = stress_verity,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_verity_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/fsverity.h or verity ioctl() commands"
};
#endif
