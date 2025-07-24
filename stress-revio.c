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
#include "core-pragma.h"

#define MIN_REVIO_BYTES		(1 * MB)
#define MAX_REVIO_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_REVIO_BYTES	(1 * GB)

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

/* Other modes */
#define REVIO_OPT_UTIMES	(0x00100000)
#define REVIO_OPT_FSYNC		(0x00200000)
#define REVIO_OPT_FDATASYNC	(0x00400000)
#define REVIO_OPT_SYNCFS	(0x00800000)
#define REVIO_OPT_READAHEAD	(0x01000000)

static const stress_help_t help[] = {
	{ NULL,	"revio N",		"start N workers performing reverse I/O" },
	{ NULL, "revio-bytes N",	"specify file size (default is 1GB)" },
	{ NULL,	"revio-ops N",		"stop after N revio bogo operations" },
	{ NULL, "revio-opts list",	"specify list of various stressor options" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	const char *opt;	/* User option */
	const int flag;		/* REVIO_OPT_ flag */
	const int exclude;	/* Excluded REVIO_OPT_ flags */
	const int advice;	/* posix_fadvise value */	/* cppcheck-suppress unusedStructMember */
	const int oflag;	/* open O_* flags */
} stress_revio_opts_t;

static const stress_revio_opts_t revio_opts[] = {
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
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_NORMAL)
	{ "fadv-normal",REVIO_OPT_FADV_NORMAL,
		(REVIO_OPT_FADV_SEQ | REVIO_OPT_FADV_RND |
		 REVIO_OPT_FADV_NOREUSE | REVIO_OPT_FADV_WILLNEED |
		 REVIO_OPT_FADV_DONTNEED),
		POSIX_FADV_NORMAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_SEQUENTIAL)
	{ "fadv-seq",	REVIO_OPT_FADV_SEQ,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_RND),
		POSIX_FADV_SEQUENTIAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_RANDOM)
	{ "fadv-rnd",	REVIO_OPT_FADV_RND,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_SEQ),
		POSIX_FADV_RANDOM, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_NOREUSE)
	{ "fadv-noreuse", REVIO_OPT_FADV_NOREUSE,
		REVIO_OPT_FADV_NORMAL,
		POSIX_FADV_NOREUSE, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_WILLNEED)
	{ "fadv-willneed", REVIO_OPT_FADV_WILLNEED,
		(REVIO_OPT_FADV_NORMAL | REVIO_OPT_FADV_DONTNEED),
		POSIX_FADV_WILLNEED, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
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
#if defined(HAVE_READAHEAD)
	{ "readahead",	REVIO_OPT_READAHEAD, 0, 0, 0 },
#endif
#if defined(HAVE_SYNCFS)
	{ "syncfs",	REVIO_OPT_SYNCFS, 0, 0, 0 },
#endif
	{ "utimes",	REVIO_OPT_UTIMES, 0, 0, 0 },
};

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
	if (UNLIKELY(!stress_continue_flag()))
		return ret;

#if defined(HAVE_FSYNC)
	if (revio_flags & REVIO_OPT_FSYNC)
		(void)shim_fsync(fd);
	if (UNLIKELY(!stress_continue_flag()))
		return ret;
#endif
#if defined(HAVE_FDATASYNC)
	if (revio_flags & REVIO_OPT_FDATASYNC)
		(void)shim_fdatasync(fd);
	if (UNLIKELY(!stress_continue_flag()))
		return ret;
#endif
#if defined(HAVE_SYNCFS)
	if (revio_flags & REVIO_OPT_SYNCFS)
		(void)syncfs(fd);
	if (UNLIKELY(!stress_continue_flag()))
		return ret;
#endif
	return ret;
}

/*
 *  stress_revio_opts
 *	parse --revio-opts option(s) list
 */
static void stress_revio_opts(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	char *str, *ptr;
	const char *token;
	int revio_flags = 0;
	int revio_oflags = 0;
	bool opts_set = false;

	(void)type_id;
	(void)value;

	str = stress_const_optdup(opt_arg);
	if (!str) {
		(void)fprintf(stderr, "%s option: cannot dup string '%s'\n", opt_name, opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		size_t i;
		bool opt_ok = false;

		for (i = 0; i < SIZEOF_ARRAY(revio_opts); i++) {
			if (!strcmp(token, revio_opts[i].opt)) {
				const int exclude = revio_flags & revio_opts[i].exclude;

				if (exclude) {
					int j;

					for (j = 0; revio_opts[j].opt; j++) {
						if ((exclude & revio_opts[j].flag) == exclude) {
							(void)fprintf(stderr,
								"%s option '%s' is not "
								"compatible with option '%s'\n",
								opt_name, token,
								revio_opts[j].opt);
							free(str);
							longjmp(g_error_env, 1);
							stress_no_return();
						}
					}
					free(str);
					return;
				}
				revio_flags  |= revio_opts[i].flag;
				revio_oflags |= revio_opts[i].oflag;
				opt_ok = true;
				opts_set = true;
			}
		}
		if (!opt_ok) {
			(void)fprintf(stderr, "%s option '%s' not known, options are:", opt_name, token);
			for (i = 0; i < SIZEOF_ARRAY(revio_opts); i++)
				(void)fprintf(stderr, " %s", revio_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
	}

	stress_set_setting("revio", "revio-flags", TYPE_ID_INT, &revio_flags);
	stress_set_setting("revio", "revio-oflags", TYPE_ID_INT, &revio_oflags);
	stress_set_setting("revio", "revio-opts-set", TYPE_ID_BOOL, &opts_set);
	free(str);
}

/*
 *  stress_revio_advise()
 *	set posix_fadvise options
 */
static int stress_revio_advise(stress_args_t *args, const int fd, const int flags)
{
#if (defined(POSIX_FADV_SEQ) || defined(POSIX_FADV_RANDOM) ||		\
     defined(POSIX_FADV_NOREUSE) || defined(POSIX_FADV_WILLNEED) ||	\
     defined(POSIX_FADV_DONTNEED)) &&					\
    defined(HAVE_POSIX_FADVISE)
	size_t i;

	if (!(flags & REVIO_OPT_FADV_MASK))
		return 0;

	for (i = 0; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(revio_opts))); i++) {
		if (revio_opts[i].flag & flags) {
			if (posix_fadvise(fd, 0, 0, revio_opts[i].advice) < 0) {
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
 *  stress_revio
 *	stress I/O via writes in reverse
 */
static int stress_revio(stress_args_t *args)
{
	uint8_t *buf = NULL;
	void *alloc_buf;
	uint64_t i;
	int rc = EXIT_FAILURE;
	ssize_t ret;
	char filename[PATH_MAX];
	size_t opt_index = 0;
	uint64_t revio_bytes, revio_bytes_total = DEFAULT_REVIO_BYTES;
	uint32_t iterations = 0;
	int revio_flags = 0, revio_oflags = 0;
	int flags, fadvise_flags;
	bool opts_set = false;
	double avg_extents = 0.0;

	(void)stress_get_setting("revio-flags", &revio_flags);
	(void)stress_get_setting("revio-oflags", &revio_oflags);
	(void)stress_get_setting("revio-opts-set", &opts_set);

	revio_flags |= REVIO_OPT_O_DIRECT;	/* HACK */

	flags = O_CREAT | O_RDWR | O_TRUNC | revio_oflags;
	fadvise_flags = revio_flags & REVIO_OPT_FADV_MASK;

	if (!stress_get_setting("revio-bytes", &revio_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			revio_bytes_total = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			revio_bytes_total = MIN_REVIO_BYTES;
	}

	revio_bytes = revio_bytes_total / args->instances;
	/* Ensure complete file size is not less than the I/O size */
	if (revio_bytes < DEFAULT_REVIO_WRITE_SIZE) {
		revio_bytes = DEFAULT_REVIO_WRITE_SIZE;
		revio_bytes_total = revio_bytes * args->instance;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, revio_bytes, revio_bytes_total);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status((int)-ret);

#if defined(HAVE_POSIX_MEMALIGN)
	ret = posix_memalign((void **)&alloc_buf, BUF_ALIGNMENT, (size_t)DEFAULT_REVIO_WRITE_SIZE);
	if (ret || !alloc_buf) {
		rc = stress_exit_status(errno);
		pr_err("%s: failed to allocate %zu byte buffer%s\n",
			args->name, (size_t)DEFAULT_REVIO_WRITE_SIZE,
			stress_get_memfree_str());
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = alloc_buf;
#else
	/* Work around lack of posix_memalign */
	alloc_buf = malloc((size_t)DEFAULT_REVIO_WRITE_SIZE + BUF_ALIGNMENT);
	if (!alloc_buf) {
		pr_err("%s: failed to allocate %zu buffer%s\n",
			args->name, (size_t)DEFAULT_REVIO_WRITE_SIZE + BUF_ALIGNMENT,
			stress_get_memfree_str());
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = (uint8_t *)stress_align_address(alloc_buf, BUF_ALIGNMENT);
#endif

	stress_rndbuf(buf, DEFAULT_REVIO_WRITE_SIZE);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int fd;
		size_t extents;
		const char *fs_type;

		/*
		 * aggressive option with no other option enables
		 * the "work through all the options" mode
		 */
		if (!opts_set && (g_opt_flags & OPT_FLAGS_AGGRESSIVE)) {
			opt_index++;
			if (opt_index >= SIZEOF_ARRAY(revio_opts))
				opt_index = 0;

			revio_flags = revio_opts[opt_index].flag;
			revio_oflags = revio_opts[opt_index].oflag;
		}

		if (UNLIKELY((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0)) {
			if ((errno == ENOSPC) || (errno == ENOMEM))
				continue;	/* Retry */
			pr_fail("%s: open %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto finish;
		}
		stress_file_rw_hint_short(fd);

		fs_type = stress_get_fs_type(filename);
		(void)shim_unlink(filename);
		if (UNLIKELY(ftruncate(fd, (off_t)revio_bytes) < 0)) {
			pr_fail("%s: ftruncate failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			(void)close(fd);
			rc = EXIT_FAILURE;
			goto finish;
		}

		if (UNLIKELY(stress_revio_advise(args, fd, fadvise_flags) < 0)) {
			(void)close(fd);
			rc = EXIT_FAILURE;
			goto finish;
		}

		/* Sequential Reverse Write */
		for (i = 0; i < revio_bytes; i += DEFAULT_REVIO_WRITE_SIZE * (8 + (stress_mwc8() & 7))) {
			size_t j;
			off_t lseek_ret;
			const off_t offset = (off_t)(revio_bytes - i);
seq_wr_retry:
			if (UNLIKELY(!stress_continue(args)))
				break;

			lseek_ret = lseek(fd, offset, SEEK_SET);
			if (UNLIKELY(lseek_ret < 0)) {
				pr_fail("%s: write failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				(void)close(fd);
				rc = EXIT_FAILURE;
				goto finish;
			}

PRAGMA_UNROLL_N(4)
			for (j = 0; j < DEFAULT_REVIO_WRITE_SIZE; j += 512)
				buf[j] = (i * j) & 0xff;
			ret = stress_revio_write(fd, buf, (size_t)DEFAULT_REVIO_WRITE_SIZE, revio_flags);
			if (UNLIKELY(ret <= 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					goto seq_wr_retry;
				if (errno == ENOSPC)
					break;
				if (errno) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					(void)close(fd);
					rc = EXIT_FAILURE;
					goto finish;
				}
				continue;
			}
			stress_bogo_inc(args);
		}
		iterations++;
		extents = stress_get_extents(fd);
		avg_extents += (double)extents;
#if defined(HAVE_READAHEAD)
		if (revio_flags & REVIO_OPT_READAHEAD) {
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
			VOID_RET(int, posix_fadvise(fd, 0, revio_bytes, POSIX_FADV_DONTNEED));
#endif
			if (lseek(fd, 0, SEEK_SET) == 0)
				(void)readahead(fd, 0, revio_bytes);
		}
#endif
		(void)close(fd);
	} while (stress_continue(args));

	if ((iterations > 0) && (avg_extents > 0.0)) {
		avg_extents /= (double)iterations;
		stress_metrics_set(args, 0, "extents",
			(double)avg_extents, STRESS_METRIC_GEOMETRIC_MEAN);
	}

	rc = EXIT_SUCCESS;
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(alloc_buf);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}


static const stress_opt_t opts[] = {
	{ OPT_revio_bytes, "revio-bytes", TYPE_ID_UINT64_BYTES_VM, MIN_REVIO_BYTES, MAX_REVIO_BYTES, NULL },
	{ OPT_revio_opts,  "revio-opts",  TYPE_ID_CALLBACK, 0, 0, stress_revio_opts },
	END_OPT,
};

const stressor_info_t stress_revio_info = {
	.stressor = stress_revio,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
