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

#define BUF_ALIGNMENT		(4096)

#define DEFAULT_REVIO_WRITE_SIZE (1024)

/* POSIX fadvise modes */
#define REVIO_OPT_FADV_NORMAL	(0x00000100)
#define REVIO_OPT_FADV_SEQ	(0x00000200)
#define REVIO_OPT_FADV_RND	(0x00000400)
#define REVIO_OPT_FADV_NOREUSE	(0x00000800)
#define REVIO_OPT_FADV_WILLNEED	(0x00001000)
#define REVIO_OPT_FADV_DONTNEED	(0x00002000)
#define REVIO_OPT_FADV_MASK	(0x00003f00)

/* Open O_* modes */
#define REVIO_OPT_O_SYNC	(0x00010000)
#define REVIO_OPT_O_DSYNC	(0x00020000)
#define REVIO_OPT_O_DIRECT	(0x00040000)
#define REVIO_OPT_O_NOATIME	(0x00080000)
#define REVIO_OPT_O_MASK	(0x000f0000)

/* Other modes */
#define REVIO_OPT_UTIMES	(0x00100000)
#define REVIO_OPT_FSYNC		(0x00200000)
#define REVIO_OPT_FDATASYNC	(0x00400000)
#define REVIO_OPT_SYNCFS	(0x00800000)

static const help_t help[] = {
	{ NULL,	"revio N",	"start N workers performing reverse I/O" },
	{ NULL,	"revio-ops N",	"stop after N revio bogo operations" },
	{ NULL,	NULL,		NULL }
};

typedef struct {
	const char *opt;	/* User option */
	const int flag;		/* REVIO_OPT_ flag */
	const int exclude;	/* Excluded REVIO_OPT_ flags */
	const int advice;	/* posix_fadvise value */
	const int oflag;	/* open O_* flags */
} revio_opts_t;

static const revio_opts_t revio_opts[] = {
#if defined(O_SYNC)
	{ "sync",	REVIO_OPT_O_SYNC, 0, 0, O_SYNC },
#endif
#if defined(O_DSYNC)
	{ "dsync",	REVIO_OPT_O_DSYNC, 0, 0, O_DSYNC },
#endif
#if defined(O_DIRECT)
	{ "direct",	REVIO_OPT_O_DIRECT, 0, 0, O_DIRECT },
#endif
#if defined(O_NOATIME)
	{ "noatime",	REVIO_OPT_O_NOATIME, 0, 0, O_NOATIME },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_NORMAL)
	{ "fadv-normal",REVIO_OPT_FADV_NORMAL,
		(REVIO_OPT_FADV_SEQ | REVIO_OPT_FADV_RND |
		 REVIO_OPT_FADV_NOREUSE | REVIO_OPT_FADV_WILLNEED |
		 REVIO_OPT_FADV_DONTNEED),
		POSIX_FADV_NORMAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_SEQ)
	{ "fadv-seq",	REVIO_OPT_FADV_SEQ,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_RND),
		POSIX_FADV_SEQUENTIAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_RANDOM)
	{ "fadv-rnd",	REVIO_OPT_FADV_RND,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_SEQ),
		POSIX_FADV_RANDOM, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_NOREUSE)
	{ "fadv-noreuse", REVIO_OPT_FADV_NOREUSE,
		REVIO_OPT_FADV_NORMAL,
		POSIX_FADV_NOREUSE, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_WILLNEED)
	{ "fadv-willneed", REVIO_OPT_FADV_WILLNEED,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_DONTNEED),
		POSIX_FADV_WILLNEED, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
	{ "fadv-dontneed", REVIO_OPT_FADV_DONTNEED,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_WILLNEED),
		POSIX_FADV_DONTNEED, 0 },
#endif
#if defined(HAVE_FSYNC)
	{ "fsync",	REVIO_OPT_FSYNC, 0, 0, 0 },
#endif
#if defined(HAVE_FDATASYNC)
	{ "fdatasync",	REVIO_OPT_FDATASYNC, 0, 0, 0 },
#endif
#if defined(HAVE_SYNCFS)
	{ "syncfs",	REVIO_OPT_SYNCFS, 0, 0, 0 },
#endif
	{ "utimes",	REVIO_OPT_UTIMES, 0, 0, 0 },
};

static int stress_set_revio_bytes(const char *opt)
{
	uint64_t revio_bytes;

	revio_bytes = get_uint64_byte_filesystem(opt, 1);
	check_range_bytes("revio-bytes", revio_bytes,
		MIN_REVIO_BYTES, MAX_REVIO_BYTES);
	return set_setting("revio-bytes", TYPE_ID_UINT64, &revio_bytes);
}

/*
 *  stress_revio_write()
 *	write with writev or write depending on mode
 */
static ssize_t stress_revio_write(
	const int fd,
	uint8_t *buf,
	const size_t count,
	const int revio_flags)
{
	ssize_t ret;

	(void)revio_flags;

#if defined(HAVE_FUTIMES)
	if (revio_flags & REVIO_OPT_UTIMES)
		(void)futimes(fd, NULL);
#endif

	ret = write(fd, buf, count);

#if defined(HAVE_FSYNC)
	if (revio_flags & REVIO_OPT_FSYNC)
		(void)shim_fsync(fd);
#endif
#if defined(HAVE_FDATASYNC)
	if (revio_flags & REVIO_OPT_FDATASYNC)
		(void)shim_fdatasync(fd);
#endif
#if defined(HAVE_SYNCFS)
	if (revio_flags & REVIO_OPT_SYNCFS)
		(void)syncfs(fd);
#endif

	return ret;
}

/*
 *  stress_set_revio_opts
 *	parse --revio-opts option(s) list
 */
static int stress_set_revio_opts(const char *opts)
{
	char *str, *ptr, *token;
	int revio_flags = 0;
	int revio_oflags = 0;
	bool opts_set = false;

	str = stress_const_optdup(opts);
	if (!str)
		return -1;

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		size_t i;
		bool opt_ok = false;

		for (i = 0; i < SIZEOF_ARRAY(revio_opts); i++) {
			if (!strcmp(token, revio_opts[i].opt)) {
				int exclude = revio_flags & revio_opts[i].exclude;
				if (exclude) {
					int j;

					for (j = 0; revio_opts[j].opt; j++) {
						if ((exclude & revio_opts[j].flag) == exclude) {
							(void)fprintf(stderr,
								"revio-opt option '%s' is not "
								"compatible with option '%s'\n",
								token,
								revio_opts[j].opt);
							break;
						}
					}
					free(str);
					return -1;
				}
				revio_flags  |= revio_opts[i].flag;
				revio_oflags |= revio_opts[i].oflag;
				opt_ok = true;
				opts_set = true;
			}
		}
		if (!opt_ok) {
			(void)fprintf(stderr, "revio-opt option '%s' not known, options are:", token);
			for (i = 0; i < SIZEOF_ARRAY(revio_opts); i++)
				(void)fprintf(stderr, "%s %s",
					i == 0 ? "" : ",", revio_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			return -1;
		}
	}

	set_setting("revio-flags", TYPE_ID_INT, &revio_flags);
	set_setting("revio-oflags", TYPE_ID_INT, &revio_oflags);
	set_setting("revio-opts-set", TYPE_ID_BOOL, &opts_set);
	free(str);

	return 0;
}

/*
 *  stress_revio_advise()
 *	set posix_fadvise options
 */
static int stress_revio_advise(const args_t *args, const int fd, const int flags)
{
#if (defined(POSIX_FADV_SEQ) || defined(POSIX_FADV_RANDOM) || \
    defined(POSIX_FADV_NOREUSE) || defined(POSIX_FADV_WILLNEED) || \
    defined(POSIX_FADV_DONTNEED)) && defined(HAVE_POSIX_FADVISE)
	size_t i;

	if (!(flags & REVIO_OPT_FADV_MASK))
		return 0;

	for (i = 0; i < SIZEOF_ARRAY(revio_opts); i++) {
		if (revio_opts[i].flag & flags) {
			if (posix_fadvise(fd, 0, 0, revio_opts[i].advice) < 0) {
				pr_fail_err("posix_fadvise");
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

static inline size_t stress_revio_get_extents(const int fd)
{
#if defined(FS_IOC_FIEMAP) && defined(HAVE_LINUX_FIEMAP_H)
	struct fiemap fiemap;

	(void)memset(&fiemap, 0, sizeof(fiemap));
	fiemap.fm_length = ~0;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, &fiemap) < 0)
		return 0;

	return fiemap.fm_mapped_extents;
#else
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_revio
 *	stress I/O via writes in reverse
 */
static int stress_revio(const args_t *args)
{
	uint8_t *buf = NULL;
	void *alloc_buf;
	uint64_t i;
	int rc = EXIT_FAILURE;
	ssize_t ret;
	char filename[PATH_MAX];
	size_t opt_index = 0;
	uint64_t revio_bytes = DEFAULT_REVIO_BYTES;
	uint32_t iterations = 0;
	int revio_flags = 0, revio_oflags = 0;
	int flags, fadvise_flags;
	bool opts_set = false;
	double avg_extents = 0.0;

	(void)get_setting("revio-flags", &revio_flags);
	(void)get_setting("revio-oflags", &revio_oflags);
	(void)get_setting("revio-opts-set", &opts_set);

	revio_flags |= REVIO_OPT_O_DIRECT;	/* HACK */

	flags = O_CREAT | O_RDWR | O_TRUNC | revio_oflags;
	fadvise_flags = revio_flags & REVIO_OPT_FADV_MASK;

	if (!get_setting("revio-bytes", &revio_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			revio_bytes = MAX_REVIO_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			revio_bytes = MIN_REVIO_BYTES;
	}

	revio_bytes /= args->num_instances;

	/* Ensure complete file size is not less than the I/O size */
	if (revio_bytes < DEFAULT_REVIO_WRITE_SIZE) {
		revio_bytes = DEFAULT_REVIO_WRITE_SIZE;
		pr_inf("%s: increasing file size to write size of %"
			PRIu64 " bytes\n",
			args->name, revio_bytes);
	}


	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

#if defined(HAVE_POSIX_MEMALIGN)
	ret = posix_memalign((void **)&alloc_buf, BUF_ALIGNMENT, (size_t)DEFAULT_REVIO_WRITE_SIZE);
	if (ret || !alloc_buf) {
		rc = exit_status(errno);
		pr_err("%s: cannot allocate buffer\n", args->name);
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = alloc_buf;
#else
	/* Work around lack of posix_memalign */
	alloc_buf = malloc((size_t)DEFAULT_REVIO_WRITE_SIZE + BUF_ALIGNMENT);
	if (!alloc_buf) {
		pr_err("%s: cannot allocate buffer\n", args->name);
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = (uint8_t *)stress_align_address(alloc_buf, BUF_ALIGNMENT);
#endif

	stress_strnrnd((char *)buf, DEFAULT_REVIO_WRITE_SIZE);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	do {
		int fd;

		/*
		 * aggressive option with no other option enables
		 * the "work through all the options" mode
		 */
		if (!opts_set && (g_opt_flags & OPT_FLAGS_AGGRESSIVE)) {
			opt_index = (opt_index + 1) % SIZEOF_ARRAY(revio_opts);

			revio_flags  = revio_opts[opt_index].flag;
			revio_oflags = revio_opts[opt_index].oflag;
		}

		if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
			if ((errno == ENOSPC) || (errno == ENOMEM))
				continue;	/* Retry */
			pr_fail_err("open");
			goto finish;
		}
		if (ftruncate(fd, (off_t)revio_bytes) < 0) {
			pr_fail_err("ftruncate");
			(void)close(fd);
			goto finish;
		}
		(void)unlink(filename);

		if (stress_revio_advise(args, fd, fadvise_flags) < 0) {
			(void)close(fd);
			goto finish;
		}

		/* Sequential Reverse Write */
		for (i = 0; i < revio_bytes; i += DEFAULT_REVIO_WRITE_SIZE * (8 + (mwc8() & 7))) {
			size_t j;
			off_t lseek_ret, offset = revio_bytes - i;
seq_wr_retry:
			if (!keep_stressing())
				break;

			lseek_ret = lseek(fd, offset, SEEK_SET);
			if (lseek_ret < 0) {
				pr_fail_err("write");
				(void)close(fd);
				goto finish;
			}

			for (j = 0; j < DEFAULT_REVIO_WRITE_SIZE; j += 512)
				buf[j] = (i * j) & 0xff;
			ret = stress_revio_write(fd, buf, (size_t)DEFAULT_REVIO_WRITE_SIZE, revio_flags);
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto seq_wr_retry;
				if (errno == ENOSPC)
					break;
				if (errno) {
					pr_fail_err("write");
					(void)close(fd);
					goto finish;
				}
				continue;
			}
			inc_counter(args);
		}
		iterations++;
		avg_extents += (double)stress_revio_get_extents(fd);
		(void)close(fd);
	} while (keep_stressing());

	if ((iterations > 0) && (avg_extents > 0.0)) {
		avg_extents /= (double)iterations;
		pr_inf("%s: average number of extents %.2f\n", args->name, avg_extents * args->num_instances);
	}

	rc = EXIT_SUCCESS;
finish:
	free(alloc_buf);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_revio_bytes,	stress_set_revio_bytes },
	{ OPT_revio_opts,	stress_set_revio_opts },
	{ 0,			NULL }

};

stressor_info_t stress_revio_info = {
	.stressor = stress_revio,
	.class = CLASS_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
