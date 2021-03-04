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

#define BUF_ALIGNMENT		(4096)
#define HDD_IO_VEC_MAX		(16)		/* Must be power of 2 */

/* Write and read stress modes */
#define HDD_OPT_WR_SEQ		(0x00000001)
#define HDD_OPT_WR_RND		(0x00000002)
#define HDD_OPT_RD_SEQ		(0x00000010)
#define HDD_OPT_RD_RND		(0x00000020)
#define HDD_OPT_WR_MASK		(0x00000003)
#define HDD_OPT_RD_MASK		(0x00000030)

/* POSIX fadvise modes */
#define HDD_OPT_FADV_NORMAL	(0x00000100)
#define HDD_OPT_FADV_SEQ	(0x00000200)
#define HDD_OPT_FADV_RND	(0x00000400)
#define HDD_OPT_FADV_NOREUSE	(0x00000800)
#define HDD_OPT_FADV_WILLNEED	(0x00001000)
#define HDD_OPT_FADV_DONTNEED	(0x00002000)
#define HDD_OPT_FADV_MASK	(0x00003f00)

/* Open O_* modes */
#define HDD_OPT_O_SYNC		(0x00010000)
#define HDD_OPT_O_DSYNC		(0x00020000)
#define HDD_OPT_O_DIRECT	(0x00040000)
#define HDD_OPT_O_NOATIME	(0x00080000)
#define HDD_OPT_O_MASK		(0x000f0000)

/* Other modes */
#define HDD_OPT_IOVEC		(0x00100000)
#define HDD_OPT_UTIMES		(0x00200000)
#define HDD_OPT_FSYNC		(0x00400000)
#define HDD_OPT_FDATASYNC	(0x00800000)
#define HDD_OPT_SYNCFS		(0x01000000)

typedef struct {
	const char *opt;	/* User option */
	const int flag;		/* HDD_OPT_ flag */
	const int exclude;	/* Excluded HDD_OPT_ flags */
	const int advice;	/* posix_fadvise value */
	const int oflag;	/* open O_* flags */
} stress_hdd_opts_t;

static const stress_help_t help[] = {
	{ "d N","hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,	"hdd-ops N",		"stop after N hdd bogo operations" },
	{ NULL,	"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,	"hdd-opts list",	"specify list of various stressor options" },
	{ NULL,	"hdd-write-size N",	"set the default write size to N bytes" },
	{ NULL, NULL,			NULL }
};

static const stress_hdd_opts_t hdd_opts[] = {
#if defined(O_SYNC)
	{ "sync",	HDD_OPT_O_SYNC, 0, 0, O_SYNC },
#endif
#if defined(O_DSYNC)
	{ "dsync",	HDD_OPT_O_DSYNC, 0, 0, O_DSYNC },
#endif
#if defined(O_DIRECT)
	{ "direct",	HDD_OPT_O_DIRECT, 0, 0, O_DIRECT },
#endif
#if defined(O_NOATIME)
	{ "noatime",	HDD_OPT_O_NOATIME, 0, 0, O_NOATIME },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_NORMAL)
	{ "wr-seq",	HDD_OPT_WR_SEQ, HDD_OPT_WR_RND, 0, 0 },
	{ "wr-rnd",	HDD_OPT_WR_RND, HDD_OPT_WR_SEQ, 0, 0 },
	{ "rd-seq",	HDD_OPT_RD_SEQ, HDD_OPT_RD_RND, 0, 0 },
	{ "rd-rnd",	HDD_OPT_RD_RND, HDD_OPT_RD_SEQ, 0, 0 },
	{ "fadv-normal",HDD_OPT_FADV_NORMAL,
		(HDD_OPT_FADV_SEQ | HDD_OPT_FADV_RND |
		 HDD_OPT_FADV_NOREUSE | HDD_OPT_FADV_WILLNEED |
		 HDD_OPT_FADV_DONTNEED),
		POSIX_FADV_NORMAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_SEQUENTIAL)
	{ "fadv-seq",	HDD_OPT_FADV_SEQ,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_RND),
		POSIX_FADV_SEQUENTIAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_RANDOM)
	{ "fadv-rnd",	HDD_OPT_FADV_RND,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_SEQ),
		POSIX_FADV_RANDOM, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_NOREUSE)
	{ "fadv-noreuse", HDD_OPT_FADV_NOREUSE,
		HDD_OPT_FADV_NORMAL,
		POSIX_FADV_NOREUSE, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_WILLNEED)
	{ "fadv-willneed", HDD_OPT_FADV_WILLNEED,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_DONTNEED),
		POSIX_FADV_WILLNEED, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
	{ "fadv-dontneed", HDD_OPT_FADV_DONTNEED,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_WILLNEED),
		POSIX_FADV_DONTNEED, 0 },
#endif
#if defined(HAVE_FSYNC)
	{ "fsync",	HDD_OPT_FSYNC, 0, 0, 0 },
#endif
#if defined(HAVE_FDATASYNC)
	{ "fdatasync",	HDD_OPT_FDATASYNC, 0, 0, 0 },
#endif
	{ "iovec",	HDD_OPT_IOVEC, 0, 0, 0 },
#if defined(HAVE_SYNCFS)
	{ "syncfs",	HDD_OPT_SYNCFS, 0, 0, 0 },
#endif
	{ "utimes",	HDD_OPT_UTIMES, 0, 0, 0 },
};

static int stress_set_hdd_bytes(const char *opt)
{
	uint64_t hdd_bytes;

	hdd_bytes = stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("hdd-bytes", hdd_bytes,
		MIN_HDD_BYTES, MAX_HDD_BYTES);
	return stress_set_setting("hdd-bytes", TYPE_ID_UINT64, &hdd_bytes);
}

static int stress_set_hdd_write_size(const char *opt)
{
	uint64_t hdd_write_size;

	hdd_write_size = stress_get_uint64_byte(opt);
	stress_check_range_bytes("hdd-write-size", hdd_write_size,
		MIN_HDD_WRITE_SIZE, MAX_HDD_WRITE_SIZE);
	return stress_set_setting("hdd-write-size", TYPE_ID_UINT64, &hdd_write_size);
}

#if defined(HAVE_FUTIMES)
static void stress_hdd_utimes(const int fd)
{
	struct timeval tv[2];

	(void)futimes(fd, NULL);

	/* Exercise illegal futimes, usec too large */
	tv[0].tv_usec = 1000001;
	tv[0].tv_sec = 0;
	tv[1].tv_usec = 1000001;
	tv[1].tv_sec = 0;
	(void)futimes(fd, tv);

	/* Exercise illegal futimes, usec too small */
	tv[0].tv_usec = -1;
	tv[0].tv_sec = -1;
	tv[1].tv_usec = -1;
	tv[1].tv_sec = -1;

	(void)futimes(fd, tv);
}
#endif

/*
 *  stress_hdd_write()
 *	write with writev or write depending on mode
 */
static ssize_t stress_hdd_write(
	const int fd,
	uint8_t *buf,
	const size_t count,
	const uint64_t hdd_write_size,
	const int hdd_flags)
{
	ssize_t ret;

#if defined(HAVE_FUTIMES)
	if (hdd_flags & HDD_OPT_UTIMES)
		stress_hdd_utimes(fd);
#endif

	if (hdd_flags & HDD_OPT_IOVEC) {
		struct iovec iov[HDD_IO_VEC_MAX];
		size_t i;
		uint8_t *data = buf;
		const uint64_t sz = hdd_write_size / HDD_IO_VEC_MAX;

		for (i = 0; i < HDD_IO_VEC_MAX; i++) {
			iov[i].iov_base = (void *)data;
			iov[i].iov_len = (size_t)sz;

			data += sz;
		}
		switch (stress_mwc8() & 3) {
#if defined(HAVE_PWRITEV2)
		case 0:
			/* 25% */
			ret = pwritev2(fd, iov, HDD_IO_VEC_MAX, -1, 0);
			break;
#endif
#if defined(HAVE_PWRITEV)
		case 1: {
				/* 25% */
				off_t offset = lseek(fd, SEEK_CUR, 0);

				if (offset != (off_t)-1) {
					ret = pwritev(fd, iov, HDD_IO_VEC_MAX, offset);
					break;
				}
			}
			CASE_FALLTHROUGH;
#endif
		default:
			ret = writev(fd, iov, HDD_IO_VEC_MAX);
			break;
		}
	} else {
		ret = write(fd, buf, count);
	}

#if defined(HAVE_FSYNC)
	if (hdd_flags & HDD_OPT_FSYNC)
		(void)shim_fsync(fd);
#endif
#if defined(HAVE_FDATASYNC)
	if (hdd_flags & HDD_OPT_FDATASYNC)
		(void)shim_fdatasync(fd);
#endif
#if defined(HAVE_SYNCFS)
	if (hdd_flags & HDD_OPT_SYNCFS)
		(void)syncfs(fd);
#endif

	return ret;
}

/*
 *  stress_hdd_read()
 *	read with readv or read depending on mode
 */
static ssize_t stress_hdd_read(
	const int fd,
	uint8_t *buf,
	const size_t count,
	const uint64_t hdd_write_size,
	const int hdd_flags)
{
#if defined(HAVE_FUTIMES)
	if (hdd_flags & HDD_OPT_UTIMES)
		stress_hdd_utimes(fd);
#endif

	if (hdd_flags & HDD_OPT_IOVEC) {
		struct iovec iov[HDD_IO_VEC_MAX];
		size_t i;
		uint8_t *data = buf;
		const uint64_t sz = hdd_write_size / HDD_IO_VEC_MAX;

		for (i = 0; i < HDD_IO_VEC_MAX; i++) {
			iov[i].iov_base = (void *)data;
			iov[i].iov_len = (size_t)sz;

			data += sz;
		}
		switch (stress_mwc8() & 3) {
#if defined(HAVE_PREADV2)
		case 0:
			/* 25% */
			return preadv2(fd, iov, HDD_IO_VEC_MAX, -1, 0);
#endif
#if defined(HAVE_PREADV)
		case 1:
			/* 25% */
			{
				off_t offset = lseek(fd, SEEK_CUR, 0);

				if (offset != (off_t)-1)
					return preadv(fd, iov, HDD_IO_VEC_MAX, offset);
			}
			CASE_FALLTHROUGH;
#endif
		default:
			/* 50% */
			return readv(fd, iov, HDD_IO_VEC_MAX);
		}
	} else {
		return read(fd, buf, count);
	}
}


/*
 *  stress_set_hdd_opts
 *	parse --hdd-opts option(s) list
 */
static int stress_set_hdd_opts(const char *opts)
{
	char *str, *ptr, *token;
	int hdd_flags = 0;
	int hdd_oflags = 0;
	bool opts_set = false;

	str = stress_const_optdup(opts);
	if (!str)
		return -1;

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		size_t i;
		bool opt_ok = false;

		for (i = 0; i < SIZEOF_ARRAY(hdd_opts); i++) {
			if (!strcmp(token, hdd_opts[i].opt)) {
				int exclude = hdd_flags & hdd_opts[i].exclude;
				if (exclude) {
					int j;

					for (j = 0; hdd_opts[j].opt; j++) {
						if ((exclude & hdd_opts[j].flag) == exclude) {
							(void)fprintf(stderr,
								"hdd-opt option '%s' is not "
								"compatible with option '%s'\n",
								token,
								hdd_opts[j].opt);
							break;
						}
					}
					free(str);
					return -1;
				}
				hdd_flags  |= hdd_opts[i].flag;
				hdd_oflags |= hdd_opts[i].oflag;
				opt_ok = true;
				opts_set = true;
			}
		}
		if (!opt_ok) {
			(void)fprintf(stderr, "hdd-opt option '%s' not known, options are:", token);
			for (i = 0; i < SIZEOF_ARRAY(hdd_opts); i++)
				(void)fprintf(stderr, "%s %s",
					i == 0 ? "" : ",", hdd_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			return -1;
		}
	}

	stress_set_setting("hdd-flags", TYPE_ID_INT, &hdd_flags);
	stress_set_setting("hdd-oflags", TYPE_ID_INT, &hdd_oflags);
	stress_set_setting("hdd-opts-set", TYPE_ID_BOOL, &opts_set);
	free(str);

	return 0;
}

/*
 *  stress_hdd_advise()
 *	set posix_fadvise options
 */
static int stress_hdd_advise(const stress_args_t *args, const int fd, const int flags)
{
#if (defined(POSIX_FADV_SEQ) || defined(POSIX_FADV_RANDOM) || \
    defined(POSIX_FADV_NOREUSE) || defined(POSIX_FADV_WILLNEED) || \
    defined(POSIX_FADV_DONTNEED)) && defined(HAVE_POSIX_FADVISE)
	size_t i;

	if (!(flags & HDD_OPT_FADV_MASK))
		return 0;

	for (i = 0; i < SIZEOF_ARRAY(hdd_opts); i++) {
		if (hdd_opts[i].flag & flags) {
			if (posix_fadvise(fd, 0, 0, hdd_opts[i].advice) < 0) {
				pr_fail("%s: posix_fadvise failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return -1;
			}
		}
	}
#else
	(void)args;
	(void)fd;
	(void)flags;
#endif
	return 0;
}
/*
 *  stress_hdd
 *	stress I/O via writes
 */
static int stress_hdd(const stress_args_t *args)
{
	uint8_t *buf = NULL;
	void *alloc_buf;
	uint64_t i, min_size, size_remainder;
	int rc = EXIT_FAILURE;
	ssize_t ret;
	char filename[PATH_MAX];
	size_t opt_index = 0;
	uint64_t hdd_bytes = DEFAULT_HDD_BYTES;
	uint64_t hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
	int hdd_flags = 0, hdd_oflags = 0;
	int flags, fadvise_flags;
	bool opts_set = false;

	(void)stress_get_setting("hdd-flags", &hdd_flags);
	(void)stress_get_setting("hdd-oflags", &hdd_oflags);
	(void)stress_get_setting("hdd-opts-set", &opts_set);

	flags = O_CREAT | O_RDWR | O_TRUNC | hdd_oflags;
	fadvise_flags = hdd_flags & HDD_OPT_FADV_MASK;

	if (!stress_get_setting("hdd-bytes", &hdd_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			hdd_bytes = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			hdd_bytes = MIN_HDD_BYTES;
	}

	hdd_bytes /= args->num_instances;
	if (hdd_bytes < MIN_HDD_WRITE_SIZE)
		hdd_bytes = MIN_HDD_WRITE_SIZE;

	if (!stress_get_setting("hdd-write-size", &hdd_write_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			hdd_write_size = MAX_HDD_WRITE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			hdd_write_size = MIN_HDD_WRITE_SIZE;
	}

	if (hdd_flags & HDD_OPT_O_DIRECT) {
		min_size = (hdd_flags & HDD_OPT_IOVEC) ?
			HDD_IO_VEC_MAX * BUF_ALIGNMENT : MIN_HDD_WRITE_SIZE;
	} else {
		min_size = (hdd_flags & HDD_OPT_IOVEC) ?
			HDD_IO_VEC_MAX * MIN_HDD_WRITE_SIZE : MIN_HDD_WRITE_SIZE;
	}
	/* Ensure I/O size is not too small */
	if (hdd_write_size < min_size) {
		hdd_write_size = min_size;
		pr_inf("%s: increasing read/write size to %"
			PRIu64 " bytes\n", args->name, hdd_write_size);
	}

	/* Ensure we get same sized iovec I/O sizes */
	size_remainder = hdd_write_size % HDD_IO_VEC_MAX;
	if ((hdd_flags & HDD_OPT_IOVEC) && (size_remainder != 0)) {
		hdd_write_size += HDD_IO_VEC_MAX - size_remainder;
		pr_inf("%s: increasing read/write size to %"
			PRIu64 " bytes in iovec mode\n",
			args->name, hdd_write_size);
	}

	/* Ensure complete file size is not less than the I/O size */
	if (hdd_bytes < hdd_write_size) {
		hdd_bytes = hdd_write_size;
		pr_inf("%s: increasing file size to write size of %"
			PRIu64 " bytes\n",
			args->name, hdd_bytes);
	}


	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	/* Must have some write option */
	if ((hdd_flags & HDD_OPT_WR_MASK) == 0)
		hdd_flags |= HDD_OPT_WR_SEQ;
	/* Must have some read option */
	if ((hdd_flags & HDD_OPT_RD_MASK) == 0)
		hdd_flags |= HDD_OPT_RD_SEQ;

#if defined(HAVE_POSIX_MEMALIGN)
	ret = posix_memalign((void **)&alloc_buf, BUF_ALIGNMENT, (size_t)hdd_write_size);
	if (ret || !alloc_buf) {
		rc = exit_status(errno);
		pr_err("%s: cannot allocate buffer\n", args->name);
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = alloc_buf;
#else
	/* Work around lack of posix_memalign */
	alloc_buf = malloc((size_t)hdd_write_size + BUF_ALIGNMENT);
	if (!alloc_buf) {
		pr_err("%s: cannot allocate buffer\n", args->name);
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = (uint8_t *)stress_align_address(alloc_buf, BUF_ALIGNMENT);
#endif

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int fd;
		struct stat statbuf;
		uint64_t hdd_read_size;

		/*
		 * aggressive option with no other option enables
		 * the "work through all the options" mode
		 */
		if (!opts_set && (g_opt_flags & OPT_FLAGS_AGGRESSIVE)) {
			opt_index = (opt_index + 1) % SIZEOF_ARRAY(hdd_opts);

			hdd_flags = hdd_opts[opt_index].flag;
			hdd_oflags = hdd_opts[opt_index].oflag;
			if ((hdd_flags & HDD_OPT_WR_MASK) == 0)
				hdd_flags |= HDD_OPT_WR_SEQ;
			if ((hdd_flags & HDD_OPT_RD_MASK) == 0)
				hdd_flags |= HDD_OPT_RD_SEQ;
		}

		if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
			if ((errno == ENOSPC) || (errno == ENOMEM))
				continue;	/* Retry */
			pr_fail("%s: open %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto finish;
		}

#if defined(F_SET_FILE_RW_HINT) &&	\
    defined(RWH_WRITE_LIFE_SHORT)
		{
			uint64_t hint = RWH_WRITE_LIFE_SHORT;
			ret = fcntl(fd, F_SET_FILE_RW_HINT, &hint);
			(void)ret;
		}
#endif

		/* Exercise ftruncate or truncate */
		if (stress_mwc1()) {
			if (ftruncate(fd, (off_t)0) < 0) {
				pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				goto finish;
			}
			/* Exercise invalid ftruncate size, EINVAL */
			ret = ftruncate(fd, (off_t)-1);
			(void)ret;

			/* Exercise invalid fd, EBADF */
			ret = ftruncate(-1, (off_t)0);
			(void)ret;
		} else {
			if (truncate(filename, (off_t)0) < 0) {
				pr_fail("%s: truncate failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				goto finish;
			}
			/* Exercise invalid truncate size, EINVAL */
			ret = truncate(filename, (off_t)-1);
			(void)ret;

			/* Exercise invalid path, ENOENT */
			ret = truncate("", (off_t)0);
			(void)ret;
		}
		(void)unlink(filename);

		if (!keep_stressing(args)) {
			(void)close(fd);
			goto yielded;
		}

		if (stress_hdd_advise(args, fd, fadvise_flags) < 0) {
			(void)close(fd);
			goto finish;
		}

		/* Random Write */
		if (hdd_flags & HDD_OPT_WR_RND) {
			for (i = 0; i < hdd_bytes; i += hdd_write_size) {
				size_t j;

				off_t offset = (i == 0) ?
					hdd_bytes :
					(stress_mwc64() % hdd_bytes) & ~511;

				if (lseek(fd, offset, SEEK_SET) < 0) {
					pr_fail("%s: lseek failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(fd);
					goto finish;
				}
rnd_wr_retry:
				if (!keep_stressing(args)) {
					(void)close(fd);
					goto yielded;
				}

				for (j = 0; j < hdd_write_size; j++)
					buf[j] = (((offset + j) >> 9) + offset + j + args->instance) & 0xff;

				ret = stress_hdd_write(fd, buf, (size_t)hdd_write_size,
					hdd_write_size, hdd_flags);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto rnd_wr_retry;
					if (errno == ENOSPC)
						break;
					if (errno) {
						pr_fail("%s: write failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				inc_counter(args);
			}
		}
		/* Sequential Write */
		if (hdd_flags & HDD_OPT_WR_SEQ) {
			for (i = 0; i < hdd_bytes; i += hdd_write_size) {
				size_t j;
seq_wr_retry:
				if (!keep_stressing(args)) {
					(void)close(fd);
					goto yielded;
				}

				for (j = 0; j < hdd_write_size; j++)
					buf[j] = (((i + j) >> 9) + i + j + args->instance) & 0xff;
				ret = stress_hdd_write(fd, buf, (size_t)hdd_write_size,
					hdd_write_size, hdd_flags);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto seq_wr_retry;
					if (errno == ENOSPC)
						break;
					if (errno) {
						pr_fail("%s: write failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				inc_counter(args);
			}
		}

		if (fstat(fd, &statbuf) < 0) {
			pr_fail("%s: fstat failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			continue;
		}
		/* Round to write size to get no partial reads */
		hdd_read_size = (uint64_t)statbuf.st_size -
			(statbuf.st_size % hdd_write_size);

		/* Sequential Read */
		if (hdd_flags & HDD_OPT_RD_SEQ) {
			uint64_t misreads = 0;
			uint64_t baddata = 0;

			if (lseek(fd, 0, SEEK_SET) < 0) {
				pr_fail("%s: lseek failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				goto finish;
			}
			for (i = 0; i < hdd_read_size; i += hdd_write_size) {
seq_rd_retry:
				if (!keep_stressing(args)) {
					(void)close(fd);
					goto yielded;
				}

				ret = stress_hdd_read(fd, buf, (size_t)hdd_write_size,
					hdd_write_size, hdd_flags);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto seq_rd_retry;
					if (errno) {
						pr_fail("%s: read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				if (ret != (ssize_t)hdd_write_size)
					misreads++;

				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					if (hdd_flags & HDD_OPT_WR_SEQ) {
						ssize_t j;

						/* Write seq has written to all of the file, so it should always be OK */
						for (j = 0; j < ret; j++) {
							register uint8_t v = (((i + j) >> 9) + i + j + args->instance) & 0xff;

							if (buf[j] != v)
								baddata++;
						}
					} else {
						ssize_t j;

						/* Write rnd has written to some of the file, so data either zero or OK */
						for (j = 0; j < ret; j++) {
							register uint8_t v = (((i + j) >> 9) + i + j + args->instance) & 0xff;

							if (buf[j] != 0 && buf[j] != v)
								baddata++;
						}
					}
				}
				inc_counter(args);
			}
			if (misreads)
				pr_dbg("%s: %" PRIu64
					" incomplete sequential reads\n",
					args->name, misreads);
			if (baddata)
				pr_fail("%s: incorrect data found %" PRIu64 " times\n",
					args->name, baddata);
		}
		/* Random Read */
		if (hdd_flags & HDD_OPT_RD_RND) {
			uint64_t misreads = 0;
			uint64_t baddata = 0;

			for (i = 0; i < hdd_read_size; i += hdd_write_size) {
				off_t offset = (hdd_bytes > hdd_write_size) ?
					(stress_mwc64() % (hdd_bytes - hdd_write_size)) & ~511 : 0;

				if (lseek(fd, offset, SEEK_SET) < 0) {
					pr_fail("%s: lseek failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(fd);
					goto finish;
				}
rnd_rd_retry:
				if (!keep_stressing(args)) {
					(void)close(fd);
					goto yielded;
				}
				ret = stress_hdd_read(fd, buf, (size_t)hdd_write_size,
					hdd_write_size, hdd_flags);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto rnd_rd_retry;
					if (errno) {
						pr_fail("%s: read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				if (ret != (ssize_t)hdd_write_size)
					misreads++;

				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					ssize_t j;

					for (j = 0; j < ret; j++) {
						uint8_t v = (((offset + j) >> 9) + offset + j + args->instance) & 0xff;
						if (hdd_flags & HDD_OPT_WR_SEQ) {
							/* Write seq has written to all of the file, so it should always be OK */
							if (buf[j] != v)
								baddata++;
						} else {
							/* Write rnd has written to some of the file, so data either zero or OK */
							if (buf[j] != 0 && buf[j] != v)
								baddata++;
						}
					}
				}
				inc_counter(args);
			}
			if (misreads)
				pr_dbg("%s: %" PRIu64
					" incomplete random reads\n",
					args->name, misreads);
		}
		(void)close(fd);

	} while (keep_stressing(args));

yielded:
	rc = EXIT_SUCCESS;
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(alloc_buf);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_hdd_bytes,	stress_set_hdd_bytes },
	{ OPT_hdd_opts,		stress_set_hdd_opts },
	{ OPT_hdd_write_size,	stress_set_hdd_write_size },
	{ 0,			NULL },
};

stressor_info_t stress_hdd_info = {
	.stressor = stress_hdd,
	.class = CLASS_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
