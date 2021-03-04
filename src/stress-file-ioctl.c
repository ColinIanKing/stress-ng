/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	{ NULL,	"file-ioctl N",		"start N workers exercising file specific ioctls" },
	{ NULL,	"file-ioctl-ops N",	"stop after N file ioctl bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if (defined(FIONBIO) && defined(O_NONBLOCK)) || \
    (defined(FIOASYNC) && defined(O_ASYNC))
static void check_flag(
	const stress_args_t *args,
	const char *ioctl_name,
	const int fd,
	const int flag,
	const int ret,
	const bool set)
{
#if defined(F_GETFL)
	if (ret == 0) {
		int flags;

		flags = fcntl(fd, F_GETFL, 0);
		/*
		 *  The fcntl failed, so checking is not a valid
		 *  thing to sanity check with.
		 */
		if (errno != 0)
			return;
		if ((set && !(flags & flag)) ||
		    (!set && (flags & flag)))
			pr_fail("%s: ioctl %s failed, unexpected flags when checked with F_GETFL\n",
				args->name, ioctl_name);
	}
#else
	(void)args;
	(void)ioctl_name;
	(void)fd;
	(void)flag;
	(void)ret;
#endif
}
#endif

/*
 *  stress_file_ioctl
 *	stress file ioctls
 */
static int stress_file_ioctl(const stress_args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;
	const int bad_fd = stress_get_bad_fd();
#if defined(FICLONE) || defined(FICLONERANGE)
	int dfd;
#endif
	const off_t file_sz = 8192;
	uint32_t rnd = stress_mwc32();

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args, filename, sizeof(filename), rnd);
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_err("%s: cannot create %s\n", args->name, filename);
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)unlink(filename);

#if defined(FICLONE) || defined(FICLONERANGE)
	(void)stress_temp_filename_args(args, filename, sizeof(filename), rnd + 1);
	dfd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (dfd < 0) {
		ret = exit_status(errno);
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		pr_err("%s: cannot create %s\n", args->name, filename);
		return ret;
	}
	(void)unlink(filename);
#endif

	(void)shim_fallocate(fd, 0, 0, file_sz);
#if defined(FICLONE) || defined(FICLONERANGE)
	(void)shim_fallocate(dfd, 0, 0, file_sz);
#endif
	(void)shim_fsync(fd);

	(void)bad_fd;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int exercised = 0;

#if defined(FIOCLEX)
		{
			ret = ioctl(fd, FIOCLEX);
			(void)ret;

			exercised++;
		}
#endif
#if defined(FIONCLEX)
		{
			ret = ioctl(fd, FIONCLEX);
			(void)ret;

			exercised++;
		}
#endif
#if defined(FIONBIO)
		{
			int opt;

			opt = 1;
			ret = ioctl(fd, FIONBIO, &opt);
#if defined(O_NONBLOCK)
			check_flag(args, "FIONBIO", fd, O_NONBLOCK, ret, true);
#else
			(void)ret;
#endif

			opt = 0;
			ret = ioctl(fd, FIONBIO, &opt);
#if defined(O_NONBLOCK)
			check_flag(args, "FIONBIO", fd, O_NONBLOCK, ret, false);
#else
			(void)ret;
#endif
			exercised++;
		}
#endif

#if defined(FIOASYNC)
		{
			int opt;

			opt = 1;
			ret = ioctl(fd, FIOASYNC, &opt);
#if defined(O_ASYNC)
			check_flag(args, "FIONASYNC", fd, O_ASYNC, ret, true);
#else
			(void)ret;
#endif

			opt = 0;
			ret = ioctl(fd, FIOASYNC, &opt);
#if defined(O_ASYNC)
			check_flag(args, "FIONASYNC", fd, O_ASYNC, ret, false);
#else
			(void)ret;
#endif
			exercised++;
		}
#endif

#if defined(FIOQSIZE)
		{
			shim_loff_t sz;
			struct stat buf;

			ret = fstat(fd, &buf);
			if (ret == 0) {
				ret = ioctl(fd, FIOQSIZE, &sz);
				if ((ret == 0) && (file_sz != buf.st_size))
					pr_fail("%s: ioctl FIOQSIZE failed, size %jd (filesize) vs %jd (reported)\n",
						args->name,
						(intmax_t)file_sz, (intmax_t)sz);
			}
			exercised++;
		}
#endif

/* Disable this at the moment, it is fragile */
#if 0
#if defined(FIFREEZE) && defined(FITHAW)
		{
			ret = ioctl(fd, FIFREEZE);
			(void)ret;
			ret = ioctl(fd, FITHAW);
			(void)ret;

			exercised++;
		}
#endif
#endif

#if defined(FIGETBSZ)
		{
			int isz;

			ret = ioctl(fd, FIGETBSZ, &isz);
			if ((ret == 0) && (isz < 1))
				pr_fail("%s: ioctl FIGETBSZ returned unusual block size %d\n",
					args->name, isz);

			exercised++;
		}
#endif

#if defined(FICLONE)
		{
			ret = ioctl(dfd, FICLONE, fd);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FICLONERANGE)
		{
			struct file_clone_range fcr;

			(void)memset(&fcr, 0, sizeof(fcr));
			fcr.src_fd = fd;
			fcr.src_offset = 0;
			fcr.src_length = file_sz;
			fcr.dest_offset = 0;

			ret = ioctl(dfd, FICLONERANGE, &fcr);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FIDEDUPERANGE)
		{
			const size_t sz = sizeof(struct file_dedupe_range) +
					  sizeof(struct file_dedupe_range_info);
			char buf[sz] ALIGNED(64);

			struct file_dedupe_range *d = (struct file_dedupe_range *)buf;

			d->src_offset = 0;
			d->src_length = file_sz;
			d->dest_count = 1;
			d->reserved1 = 0;
			d->reserved2 = 0;
			d->info[0].dest_fd = dfd;
			d->info[0].dest_offset = 0;
			/* Zero the return values */
			d->info[0].bytes_deduped = 0;
			d->info[0].status = 0;
			d->info[0].reserved = 0;
			ret = ioctl(fd, FIDEDUPERANGE, d);
			(void)ret;

			/*
			 * and exercise illegal dest_count to force an
			 * ENOMEM error
			 */
			d->dest_count = ~0;
			ret = ioctl(fd, FIDEDUPERANGE, d);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FIONREAD)
		{
			int isz = 0;

			ret = ioctl(fd, FIONREAD, &isz);
			(void)ret;

			exercised++;

			/*
			 *  exercise invalid fd
			 */
			ret = ioctl(bad_fd, FIONREAD, &isz);
			(void)ret;

			exercised++;
		}
#endif


#if defined(FIONWRITE)
		{
			int isz = 0;

			ret = ioctl(fd, FIONWRITE, &isz);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FS_IOC_GETVERSION)
		{
			int ver;

			ret = ioctl(fd, FS_IOC_GETVERSION, &ver);
			(void)ret;

			exercised++;
		}
#endif

#if defined(_IOW) && defined(__linux__)

/*
 *  These will eventually be in linux/falloc.h for libc, but
 *  define a shim version for now.
 */
struct shim_space_resv {
	int16_t		l_type;
	int16_t		l_whence;
	int64_t		l_start;
	int64_t		l_len;
	int32_t		l_sysid;
	uint32_t	l_pid;
	int32_t		l_pad[4];
};

#if !defined(FS_IOC_RESVSP) && defined(_IOW)
#define FS_IOC_RESVSP		_IOW('X', 40, struct shim_space_resv)
#endif
#if !defined(FS_IOC_UNRESVSP) && defined(_IOW)
#define FS_IOC_UNRESVSP		_IOW('X', 41, struct shim_space_resv)
#endif
#if !defined(FS_IOC_RESVSP64) && defined(_IOW)
#define FS_IOC_RESVSP64		_IOW('X', 42, struct shim_space_resv)
#endif
#if !defined(FS_IOC_UNRESVSP64) && defined(_IOW)
#define FS_IOC_UNRESVSP64	_IOW('X', 43, struct shim_space_resv)
#endif
#if !defined(FS_IOC_ZERO_RANGE) && defined(_IOW)
#define FS_IOC_ZERO_RANGE	_IOW('X', 57, struct shim_space_resv)
#endif

#if defined(FS_IOC_RESVSP)
		{
			struct shim_space_resv r;

			(void)memset(&r, 0, sizeof(r));
			r.l_whence = SEEK_SET;
			r.l_start = (int64_t)0;
			r.l_len = (int64_t)file_sz * 2;
			ret = ioctl(fd, FS_IOC_RESVSP, &r);
			(void)ret;

			if (lseek(fd, (off_t)0, SEEK_SET) != (off_t)-1) {
				(void)memset(&r, 0, sizeof(r));
				r.l_whence = SEEK_CUR;
				r.l_start = (int64_t)0;
				r.l_len = (int64_t)file_sz;
				ret = ioctl(fd, FS_IOC_RESVSP, &r);
				(void)ret;

				(void)memset(&r, 0, sizeof(r));
				r.l_whence = SEEK_END;
				r.l_start = (int64_t)0;
				r.l_len = (int64_t)1;
				ret = ioctl(fd, FS_IOC_RESVSP, &r);
				(void)ret;
			}

			exercised++;
		}
#endif

#if defined(FS_IOC_RESVSP64)
		{
			struct shim_space_resv r;

			(void)memset(&r, 0, sizeof(r));
			r.l_whence = SEEK_SET;
			r.l_start = (int64_t)0;
			r.l_len = (int64_t)file_sz * 2;

			ret = ioctl(fd, FS_IOC_RESVSP64, &r);
			(void)ret;

			if (lseek(fd, (off_t)0, SEEK_SET) != (off_t)-1) {
				(void)memset(&r, 0, sizeof(r));
				r.l_whence = SEEK_CUR;
				r.l_start = (int64_t)0;
				r.l_len = (int64_t)file_sz;
				ret = ioctl(fd, FS_IOC_RESVSP64, &r);
				(void)ret;

				(void)memset(&r, 0, sizeof(r));
				r.l_whence = SEEK_END;
				r.l_start = (int64_t)0;
				r.l_len = (int64_t)1;
				ret = ioctl(fd, FS_IOC_RESVSP64, &r);
				(void)ret;
			}

			exercised++;
		}
#endif

#if defined(FS_IOC_UNRESVSP)
		{
			struct shim_space_resv r;

			(void)memset(&r, 0, sizeof(r));
			r.l_whence = SEEK_SET;
			r.l_start = (int64_t)file_sz;
			r.l_len = (int64_t)file_sz * 2;

			ret = ioctl(fd, FS_IOC_UNRESVSP, &r);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FS_IOC_UNRESVSP64)
		{
			struct shim_space_resv r;

			(void)memset(&r, 0, sizeof(r));
			r.l_whence = SEEK_SET;
			r.l_start = (int64_t)file_sz;
			r.l_len = (int64_t)file_sz * 2;

			ret = ioctl(fd, FS_IOC_UNRESVSP64, &r);
			(void)ret;

			exercised++;
		}
#endif

#if defined(FS_IOC_ZERO_RANGE)
		{
			struct shim_space_resv r;

			(void)memset(&r, 0, sizeof(r));
			r.l_whence = SEEK_SET;
			r.l_start = (int64_t)0;
			r.l_len = (int64_t)file_sz / 2;

			ret = ioctl(fd, FS_IOC_ZERO_RANGE, &r);
			(void)ret;

			exercised++;
		}
#endif

#endif

#if defined(FIBMAP)
		{
			int block;

			block = 0;
			ret = ioctl(fd, FIBMAP, &block);
			(void)ret;

			/*
			 *  and exercise huge block request
			 *  that should return -ERANGE or -EINVAL;
			 */
			block = -1;
			ret = ioctl(fd, FIBMAP, &block);
			(void)ret;

			exercised++;
		}
#endif
		if (!exercised) {
			pr_inf("%s: no available file ioctls to exercise\n",
				args->name);
			ret = EXIT_NOT_IMPLEMENTED;
			goto tidy;
		}

		inc_counter(args);
	} while (keep_stressing(args));

	ret = EXIT_SUCCESS;

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(FICLONE) || defined(FICLONERANGE)
	(void)close(dfd);
#endif
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

stressor_info_t stress_file_ioctl_info = {
	.stressor = stress_file_ioctl,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
