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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

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
	const int advice;	/* posix_fadvise value */	/* cppcheck-suppress unusedStructMember */
	const int oflag;	/* open O_* flags */
} stress_hdd_opts_t;

static const stress_help_t help[] = {
	{ "d N","hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,	"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,	"hdd-ops N",		"stop after N hdd bogo operations" },
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
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_NORMAL)
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
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_SEQUENTIAL)
	{ "fadv-seq",	HDD_OPT_FADV_SEQ,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_RND),
		POSIX_FADV_SEQUENTIAL, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_RANDOM)
	{ "fadv-rnd",	HDD_OPT_FADV_RND,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_SEQ),
		POSIX_FADV_RANDOM, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_NOREUSE)
	{ "fadv-noreuse", HDD_OPT_FADV_NOREUSE,
		HDD_OPT_FADV_NORMAL,
		POSIX_FADV_NOREUSE, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_WILLNEED)
	{ "fadv-willneed", HDD_OPT_FADV_WILLNEED,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_DONTNEED),
		POSIX_FADV_WILLNEED, 0 },
#endif
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
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
#if defined(HAVE_SYS_UIO_H)
	{ "iovec",	HDD_OPT_IOVEC, 0, 0, 0 },
#endif
#if defined(HAVE_SYNCFS)
	{ "syncfs",	HDD_OPT_SYNCFS, 0, 0, 0 },
#endif
	{ "utimes",	HDD_OPT_UTIMES, 0, 0, 0 },
};

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
	const off_t offset,
	const uint64_t hdd_write_size,
	const int hdd_flags,
	double *hdd_write_bytes,
	double *hdd_write_duration)
{
	ssize_t ret = -1;
	double t;

#if defined(HAVE_FUTIMES)
	if (hdd_flags & HDD_OPT_UTIMES)
		stress_hdd_utimes(fd);
#else
	UNEXPECTED
#endif
	errno = 0;
	if (hdd_flags & HDD_OPT_IOVEC) {
#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PWRITEV2) ||	\
     defined(HAVE_PWRITEV) ||	\
     defined(HAVE_WRITEV))
		struct iovec iov[HDD_IO_VEC_MAX];
		size_t i;
		uint8_t *data = buf;
		const uint64_t sz = hdd_write_size / HDD_IO_VEC_MAX;
#if defined(HAVE_PWRITEV2)
		int pwitev2_flag = 0;
#endif

		for (i = 0; i < HDD_IO_VEC_MAX; i++) {
			iov[i].iov_base = (void *)data;
			iov[i].iov_len = (size_t)sz;

			data += sz;
		}

		switch (stress_mwc8modn(3)) {
#if defined(HAVE_PWRITEV2)
		case 0:
			t = stress_time_now();
#if defined(RWF_HIPRI)
			if (hdd_flags & HDD_OPT_O_DIRECT)
				pwitev2_flag |= RWF_HIPRI;
#endif
			ret = pwritev2(fd, iov, HDD_IO_VEC_MAX, offset, pwitev2_flag);
			if (ret > 0) {
				(*hdd_write_duration) += stress_time_now() - t;
				(*hdd_write_bytes) += (double)ret;
			}
			break;
#endif
#if defined(HAVE_PWRITEV)
		case 1:
			t = stress_time_now();
			ret = pwritev(fd, iov, HDD_IO_VEC_MAX, offset);
			if (ret > 0) {
				(*hdd_write_duration) += stress_time_now() - t;
				(*hdd_write_bytes) += (double)ret;
			}
			break;
#endif
#if defined(HAVE_WRITEV)
		default:
			t = stress_time_now();
			if (lseek(fd, offset, SEEK_SET) < 0) {
				ret = -1;
			} else {
				ret = writev(fd, iov, HDD_IO_VEC_MAX);
				if (ret > 0) {
					(*hdd_write_duration) += stress_time_now() - t;
					(*hdd_write_bytes) += (double)ret;
				}
			}
			break;
#endif
		}
#else
		t = stress_time_now();
		if (lseek(fd, offset, SEEK_SET) < 0) {
			ret = -1;
		} else {
			ret = write(fd, buf, (size_t)hdd_write_size);
			if (ret > 0) {
				(*hdd_write_duration) += stress_time_now() - t;
				(*hdd_write_bytes) += (double)ret;
			}
		}
#endif
	} else {
		t = stress_time_now();
		if (lseek(fd, offset, SEEK_SET) < 0) {
			ret = -1;
		} else {
			ret = write(fd, buf, (size_t)hdd_write_size);
			if (ret > 0) {
				(*hdd_write_duration) += stress_time_now() - t;
				(*hdd_write_bytes) += (double)ret;
			}
		}
	}

#if defined(HAVE_FSYNC)
	if (hdd_flags & HDD_OPT_FSYNC)
		(void)shim_fsync(fd);
#else
	UNEXPECTED
#endif
#if defined(HAVE_FDATASYNC)
	if (hdd_flags & HDD_OPT_FDATASYNC)
		(void)shim_fdatasync(fd);
#else
	UNEXPECTED
#endif
#if defined(HAVE_SYNCFS)
	if (hdd_flags & HDD_OPT_SYNCFS)
		(void)syncfs(fd);
#else
	UNEXPECTED
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
	const off_t offset,
	const uint64_t hdd_read_size,
	const int hdd_flags,
	double *hdd_read_bytes,
	double *hdd_read_duration)
{
	ssize_t ret = -1;
	double t;

#if defined(HAVE_FUTIMES)
	if (hdd_flags & HDD_OPT_UTIMES)
		stress_hdd_utimes(fd);
#else
	UNEXPECTED
#endif

	errno = 0;
	if (hdd_flags & HDD_OPT_IOVEC) {
#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PREADV2) ||	\
     defined(HAVE_PREADV) ||	\
     defined(HAVE_READV))
		struct iovec iov[HDD_IO_VEC_MAX];
		size_t i;
		uint8_t *data = buf;
		const uint64_t sz = hdd_read_size / HDD_IO_VEC_MAX;

		for (i = 0; i < HDD_IO_VEC_MAX; i++) {
			iov[i].iov_base = (void *)data;
			iov[i].iov_len = (size_t)sz;

			data += sz;
		}
		switch (stress_mwc8modn(3)) {
#if defined(HAVE_PREADV2)
		case 0:
			t = stress_time_now();
			ret = preadv2(fd, iov, HDD_IO_VEC_MAX, offset, 0);
			if (ret > 0) {
				(*hdd_read_duration) += stress_time_now() - t;
				(*hdd_read_bytes) += (double)ret;
			}
			return ret;
#endif
#if defined(HAVE_PREADV)
		case 1:
			t = stress_time_now();
			ret = preadv(fd, iov, HDD_IO_VEC_MAX, offset);
			if (ret > 0) {
				(*hdd_read_duration) += stress_time_now() - t;
				(*hdd_read_bytes) += (double)ret;
			}
			return ret;
#endif
#if defined(HAVE_READV)
		default:
			t = stress_time_now();
			if (lseek(fd, offset, SEEK_SET) < 0)
				return -1;
			ret = readv(fd, iov, HDD_IO_VEC_MAX);
			if (ret > 0) {
				(*hdd_read_duration) += stress_time_now() - t;
				(*hdd_read_bytes) += (double)ret;
			}
			return ret;
#endif
		}
#else
		t = stress_time_now();
		if (lseek(fd, offset, SEEK_SET) < 0)
			return -1;
		ret = read(fd, buf, (size_t)hdd_read_size);
		if (ret > 0) {
			(*hdd_read_duration) += stress_time_now() - t;
			(*hdd_read_bytes) += (double)ret;
		}
		return ret;
#endif
	} else {
		t = stress_time_now();
		if (lseek(fd, offset, SEEK_SET) < 0)
			return -1;
		ret = read(fd, buf, (size_t)hdd_read_size);
		if (ret > 0) {
			(*hdd_read_duration) += stress_time_now() - t;
			(*hdd_read_bytes) += (double)ret;
		}
		return ret;
	}
	return ret;
}

/*
 *  stress_hdd_invalid_read()
 *	exercise invalid reads
 */
static void stress_hdd_invalid_read(const int fd, uint8_t *buf)
{
#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PREADV2) ||	\
     defined(HAVE_PREADV) ||	\
     defined(HAVE_READV))
	struct iovec iov[HDD_IO_VEC_MAX];
	size_t i;
	uint8_t *data = buf;
#endif
	(void)fd;

#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PREADV2) ||	\
     defined(HAVE_PREADV) ||	\
     defined(HAVE_READV))
	for (i = 0; i < HDD_IO_VEC_MAX; i++) {
		iov[i].iov_base = (void *)data;
		iov[i].iov_len = (size_t)1;
		data++;
	}
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PREADV2)
	/* invalid preadv2 fd */
	VOID_RET(ssize_t, preadv2(-1, iov, HDD_IO_VEC_MAX, 0, 0));

	/* invalid preadv2 offset, don't use -1 */
	VOID_RET(ssize_t, preadv2(fd, iov, HDD_IO_VEC_MAX, -2, 0));

	/* invalid preadv2 flags */
	VOID_RET(ssize_t, preadv2(fd, iov, HDD_IO_VEC_MAX, 0, ~0));
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PREADV)
	/* invalid preadv fd */
	VOID_RET(ssize_t, preadv(-1, iov, HDD_IO_VEC_MAX, 0));

	/* invalid preadv offset */
	VOID_RET(ssize_t, preadv(fd, iov, HDD_IO_VEC_MAX, -1));
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_READV)
	/* invalid readv fd */
	VOID_RET(ssize_t, readv(-1, iov, HDD_IO_VEC_MAX));
#endif

	/* invalid read fd */
	VOID_RET(ssize_t, read(-1, buf, 1));
}

/*
 *  stress_hdd_invalid_write()
 *	exercise invalid writess
 */
static void stress_hdd_invalid_write(const int fd, uint8_t *buf)
{
#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PWRITEV2) ||	\
     defined(HAVE_PWRITEV) ||	\
     defined(HAVE_WRITEV))
	struct iovec iov[HDD_IO_VEC_MAX];
	size_t i;
	uint8_t *data = buf;
#endif
	(void)fd;

#if defined(HAVE_SYS_UIO_H) &&	\
    (defined(HAVE_PWRITEV2) ||	\
     defined(HAVE_PWRITEV) ||	\
     defined(HAVE_WRITEV))
	for (i = 0; i < HDD_IO_VEC_MAX; i++) {
		iov[i].iov_base = (void *)data;
		iov[i].iov_len = (size_t)1;
		data++;
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PWRITEV2)
	/* invalid pwritev2 fd */
	VOID_RET(ssize_t, pwritev2(-1, iov, HDD_IO_VEC_MAX, 0, 0));

	/* invalid pwritev2 offset, don't use -1 */
	VOID_RET(ssize_t, pwritev2(fd, iov, HDD_IO_VEC_MAX, -2, 0));

	/* invalid pwritev2 flags */
	VOID_RET(ssize_t, pwritev2(fd, iov, HDD_IO_VEC_MAX, 0, ~0));
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PWRITEV)
	/* invalid pwritev fd */
	VOID_RET(ssize_t, pwritev(-1, iov, HDD_IO_VEC_MAX, 0));

	/* invalid pwritev offset */
	VOID_RET(ssize_t, pwritev(fd, iov, HDD_IO_VEC_MAX, -1));
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_WRITEV)
	/* invalid writev fd */
	VOID_RET(ssize_t, writev(-1, iov, HDD_IO_VEC_MAX));
#else
	UNEXPECTED
#endif

	/* invalid write fd */
	VOID_RET(ssize_t, write(-1, buf, 1));
}

/*
 *  stress_hdd_opts
 *	parse --hdd-opts option(s) list
 */
static void stress_hdd_opts(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	char *str, *ptr, *token;
	int hdd_flags = 0;
	int hdd_oflags = 0;
	bool opts_set = false;

	(void)type_id;
	(void)value;

	str = stress_const_optdup(opt_arg);
	if (!str) {
		(void)fprintf(stderr, "%s option: cannot dup string '%s'\n",
			opt_name, opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		size_t i;
		bool opt_ok = false;

		for (i = 0; i < SIZEOF_ARRAY(hdd_opts); i++) {
			if (!strcmp(token, hdd_opts[i].opt)) {
				const int exclude = hdd_flags & hdd_opts[i].exclude;

				if (exclude) {
					int j;

					for (j = 0; hdd_opts[j].opt; j++) {
						if ((exclude & hdd_opts[j].flag) == exclude) {
							(void)fprintf(stderr,
								"%s option '%s' is not "
								"compatible with option '%s'\n",
								opt_name, token,
								hdd_opts[j].opt);
							free(str);
							longjmp(g_error_env, 1);
							stress_no_return();
						}
					}
					free(str);
					return;
				}
				hdd_flags  |= hdd_opts[i].flag;
				hdd_oflags |= hdd_opts[i].oflag;
				opt_ok = true;
				opts_set = true;
			}
		}
		if (!opt_ok) {
			(void)fprintf(stderr, "%s option '%s' not known, options are:", opt_name, token);
			for (i = 0; i < SIZEOF_ARRAY(hdd_opts); i++)
				(void)fprintf(stderr, " %s", hdd_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
	}

	stress_set_setting("hdd", "hdd-flags", TYPE_ID_INT, &hdd_flags);
	stress_set_setting("hdd", "hdd-oflags", TYPE_ID_INT, &hdd_oflags);
	stress_set_setting("hdd", "hdd-opts-set", TYPE_ID_BOOL, &opts_set);
	free(str);
}

/*
 *  stress_hdd_advise()
 *	set posix_fadvise options
 */
static int stress_hdd_advise(stress_args_t *args, const int fd, const int flags)
{
#if (defined(POSIX_FADV_SEQ) || defined(POSIX_FADV_RANDOM) ||		\
     defined(POSIX_FADV_NOREUSE) || defined(POSIX_FADV_WILLNEED) ||	\
     defined(POSIX_FADV_DONTNEED)) && 					\
     defined(HAVE_POSIX_FADVISE)
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
 *  data_value()
 *	generate 8 bit data value for offsets and instance # into a test file
 */
static inline ALWAYS_INLINE uint8_t CONST OPTIMIZE3 data_value(
	const uint64_t i,
	uint64_t j,
	const uint32_t instance)
{
	return (uint8_t)(((i + j) >> 9) + i + j + instance);
}

static void OPTIMIZE3 TARGET_CLONES hdd_fill_buf(
	uint8_t *buf,
	const uint64_t buf_size,
	const uint64_t i,
	const uint32_t instance)
{
	register uint64_t j;

	for (j = 0; j < buf_size; j++) {
		buf[j] = data_value(i, j, instance);
	}
}

/*
 *  stress_hdd
 *	stress I/O via writes
 */
static int stress_hdd(stress_args_t *args)
{
	uint8_t *buf = NULL;
	void *alloc_buf;
	uint64_t i, min_size, size_remainder;
	int rc = EXIT_FAILURE;
	ssize_t ret;
	char filename[PATH_MAX];
	size_t opt_index = 0, max_extents = 0;
	uint64_t hdd_bytes, hdd_bytes_total = DEFAULT_HDD_BYTES;
	uint64_t hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
	const uint32_t instance = args->instance;
	int hdd_flags = 0, hdd_oflags = 0;
	int flags, fadvise_flags;
	bool opts_set = false;
	double hdd_read_bytes = 0.0, hdd_read_duration = 0.0;
	double hdd_write_bytes = 0.0, hdd_write_duration = 0.0;
	double hdd_rdwr_bytes, hdd_rdwr_duration;
	double rate;

	(void)stress_get_setting("hdd-flags", &hdd_flags);
	(void)stress_get_setting("hdd-oflags", &hdd_oflags);
	(void)stress_get_setting("hdd-opts-set", &opts_set);

	flags = O_CREAT | O_RDWR | O_TRUNC | hdd_oflags;
	fadvise_flags = hdd_flags & HDD_OPT_FADV_MASK;

	if (!stress_get_setting("hdd-bytes", &hdd_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			hdd_bytes_total = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			hdd_bytes_total = MIN_HDD_BYTES;
	}
	if (hdd_bytes_total < MIN_HDD_BYTES) {
		hdd_bytes_total = MIN_HDD_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --hdd-bytes too small, using %" PRIu64 " instead\n",
				args->name, hdd_bytes_total);
	}
	if (hdd_bytes_total > MAX_HDD_BYTES) {
		hdd_bytes_total = MAX_HDD_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --hdd-bytes too large, using %" PRIu64 " instead\n",
				args->name, hdd_bytes_total);
	}

	hdd_bytes = hdd_bytes_total / args->instances;
	if (hdd_bytes < MIN_HDD_WRITE_SIZE) {
		hdd_bytes = MIN_HDD_WRITE_SIZE;
		hdd_bytes_total = hdd_bytes * args->instances;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, hdd_bytes, hdd_bytes_total);

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
		return stress_exit_status((int)-ret);

	/* Must have some write option */
	if ((hdd_flags & HDD_OPT_WR_MASK) == 0)
		hdd_flags |= HDD_OPT_WR_SEQ;
	/* Must have some read option */
	if ((hdd_flags & HDD_OPT_RD_MASK) == 0)
		hdd_flags |= HDD_OPT_RD_SEQ;

#if defined(HAVE_POSIX_MEMALIGN)
	ret = posix_memalign((void **)&alloc_buf, BUF_ALIGNMENT, (size_t)hdd_write_size);
	if (ret || !alloc_buf) {
		rc = stress_exit_status(errno);
		pr_err("%s: cannot allocate %zu byte buffer%s\n",
			args->name, (size_t)hdd_write_size,
			stress_get_memfree_str());
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = alloc_buf;
#else
	/* Work around lack of posix_memalign */
	alloc_buf = malloc((size_t)hdd_write_size + BUF_ALIGNMENT);
	if (!alloc_buf) {
		pr_err("%s: cannot allocate %zu byte buffer%s\n",
			args->name, (size_t)hdd_write_size + BUF_ALIGNMENT,
			stress_get_memfree_str());
		(void)stress_temp_dir_rm_args(args);
		return rc;
	}
	buf = (uint8_t *)stress_align_address(alloc_buf, BUF_ALIGNMENT);
#endif
	(void)shim_memset(buf, stress_mwc8(), hdd_write_size);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int fd;
		struct stat statbuf;
		uint64_t hdd_bytes_max = 0;
		const char *fs_type;
		size_t extents;

		/*
		 * aggressive option with no other option enables
		 * the "work through all the options" mode
		 */
		if (!opts_set && (g_opt_flags & OPT_FLAGS_AGGRESSIVE)) {
			opt_index++;
			if (opt_index >= SIZEOF_ARRAY(hdd_opts))
				opt_index = 0;

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
			/*
			 *  Unlink is necessary as Linux can leave stale files
			 *  on a O_DIRECT open failure if the file system does
			 *  not support O_DIRECT:
			 *  https://bugzilla.kernel.org/show_bug.cgi?id=213041
			 */
			(void)shim_unlink(filename);
			goto finish;
		}

		fs_type = stress_get_fs_type(filename);

		stress_file_rw_hint_short(fd);

		/* Exercise ftruncate or truncate */
		if (stress_mwc1()) {
			if (ftruncate(fd, (off_t)0) < 0) {
				pr_fail("%s: ftruncate failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				(void)close(fd);
				goto finish;
			}
			/* Exercise invalid ftruncate size, EINVAL */
			VOID_RET(int, ftruncate(fd, (off_t)-1));

			/* Exercise invalid fd, EBADF */
			VOID_RET(int, ftruncate(-1, (off_t)0));
		} else {
			if (truncate(filename, (off_t)0) < 0) {
				pr_fail("%s: truncate failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				(void)close(fd);
				goto finish;
			}
			/* Exercise invalid truncate size, EINVAL */
			VOID_RET(int, truncate(filename, (off_t)-1));

			/* Exercise invalid path, ENOENT */
			VOID_RET(int, truncate("", (off_t)0));
		}
		(void)shim_unlink(filename);

		if (UNLIKELY(!stress_continue(args))) {
			(void)close(fd);
			goto yielded;
		}

		if (stress_hdd_advise(args, fd, fadvise_flags) < 0) {
			(void)close(fd);
			goto finish;
		}

		/* exercise invalid I/O calls */
		stress_hdd_invalid_write(fd, buf);
		stress_hdd_invalid_read(fd, buf);

		/* Random Write */
		if (hdd_flags & HDD_OPT_WR_RND) {
			const uint32_t w = stress_mwc32();
			const uint32_t z = stress_mwc32();

			stress_mwc_set_seed(w, z);

			for (i = 0; i < hdd_bytes; i += hdd_write_size) {
				const uint64_t offset = (i == 0) ?
					hdd_bytes : stress_mwc64modn(hdd_bytes) & ~511UL;
rnd_wr_retry:
				if (UNLIKELY(!stress_continue(args))) {
					(void)close(fd);
					goto yielded;
				}

				hdd_fill_buf(buf, hdd_write_size, offset, instance);

				ret = stress_hdd_write(fd, buf, (off_t)offset,
					hdd_write_size, hdd_flags,
					&hdd_write_bytes, &hdd_write_duration);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto rnd_wr_retry;
					if (errno == ENOSPC)
						break;
					if (errno) {
						pr_fail("%s: write failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno), fs_type);
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				stress_bogo_inc(args);
				if (offset > hdd_bytes_max)
					hdd_bytes_max = offset;
			}
			stress_mwc_set_seed(w, z);
		}
		/* Sequential Write */
		if (hdd_flags & HDD_OPT_WR_SEQ) {
			for (i = 0; i < hdd_bytes; i += hdd_write_size) {
seq_wr_retry:
				if (UNLIKELY(!stress_continue(args))) {
					(void)close(fd);
					goto yielded;
				}

				hdd_fill_buf(buf, hdd_write_size, i, instance);

				errno = 0;
				ret = stress_hdd_write(fd, buf, (off_t)i,
					hdd_write_size, hdd_flags,
					&hdd_write_bytes, &hdd_write_duration);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto seq_wr_retry;
					if (errno == ENOSPC)
						break;
					if (errno) {
						pr_fail("%s: write failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno), fs_type);
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				/* max size is offset + bytes written */
				hdd_bytes_max = i + ret;
				stress_bogo_inc(args);
			}
		}
		if (shim_fstat(fd, &statbuf) < 0) {
			pr_fail("%s: fstat failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			(void)close(fd);
			continue;
		}

		/* Sequential Read */
		if (hdd_flags & HDD_OPT_RD_SEQ) {
			uint64_t misreads = 0;
			uint64_t baddata = 0;

			for (i = 0; i < hdd_bytes_max; i += hdd_write_size) {
seq_rd_retry:
				if (UNLIKELY(!stress_continue(args))) {
					(void)close(fd);
					goto yielded;
				}
				ret = stress_hdd_read(fd, buf, (off_t)i,
					hdd_write_size, hdd_flags,
					&hdd_read_bytes, &hdd_read_duration);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto seq_rd_retry;
					if (errno == ENOSPC)	/* e.g. on vfat */
						continue;
					if (errno) {
						pr_fail("%s: read failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno), fs_type);
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				if (ret != (ssize_t)hdd_write_size) {
					misreads++;
				}

				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					if (hdd_flags & HDD_OPT_WR_SEQ) {
						size_t j;

						/* Write seq has written to all of the file, so it should always be OK */
						for (j = 0; j < (size_t)ret; j++) {
							register const uint8_t v = data_value(i, j, instance);

							baddata += (buf[j] != v);
						}
					} else {
						size_t j;

						/* Write rnd has written to some of the file, so data either zero or OK */
						for (j = 0; j < (size_t)ret; j++) {
							register const uint8_t v = data_value(i, j, instance);

							baddata += ((buf[j] != 0) && (buf[j] != v));
						}
					}
				}
				stress_bogo_inc(args);
				if (i > hdd_bytes_max)
					hdd_bytes_max = i;
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

			for (i = 0; i < hdd_bytes_max; i += hdd_write_size) {
				const size_t offset = (hdd_bytes > hdd_write_size) ?
					stress_mwc64modn(hdd_bytes - hdd_write_size) & ~511UL : 0;

				if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
					pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					(void)close(fd);
					goto finish;
				}
rnd_rd_retry:
				if (UNLIKELY(!stress_continue(args))) {
					(void)close(fd);
					goto yielded;
				}
				ret = stress_hdd_read(fd, buf, (off_t)offset,
					hdd_write_size, hdd_flags,
					&hdd_read_bytes, &hdd_read_duration);
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						goto rnd_rd_retry;
					if (errno == ENOSPC)	/* e.g. on vfat */
						continue;
					if (errno) {
						pr_fail("%s: read failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno), fs_type);
						(void)close(fd);
						goto finish;
					}
					continue;
				}
				if (ret != (ssize_t)hdd_write_size)
					misreads++;

				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					size_t j;

					for (j = 0; j < (size_t)ret; j++) {
						register const uint8_t v = data_value(offset, j, instance);

						if (hdd_flags & HDD_OPT_WR_SEQ) {
							/* Write seq has written to all of the file, so it should always be OK */
							if (buf[j] != v)
								baddata++;
						} else {
							/* Write rnd has written to some of the file, so data either zero or OK */
							if ((buf[j] != 0) && (buf[j] != v))
								baddata++;
						}
					}
				}
				stress_bogo_inc(args);
			}
			if (misreads)
				pr_dbg("%s: %" PRIu64
					" incomplete random reads\n",
					args->name, misreads);
			if (baddata)
				pr_fail("%s: incorrect data found %" PRIu64 " times\n",
					args->name, baddata);
		}
		extents = stress_get_extents(fd);
		if (extents > max_extents)
			max_extents = extents;

		(void)close(fd);
	} while (stress_continue(args));

yielded:
	rc = EXIT_SUCCESS;
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (hdd_read_duration > 0.0) ? hdd_read_bytes / hdd_read_duration : 0.0;
	stress_metrics_set(args, 0, "MB/sec read rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);
	rate = (hdd_write_duration > 0.0) ? hdd_write_bytes / hdd_write_duration : 0.0;
	stress_metrics_set(args, 1, "MB/sec write rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

	hdd_rdwr_duration = hdd_read_duration + hdd_write_duration;
	hdd_rdwr_bytes = hdd_read_bytes + hdd_write_bytes;

	rate = (hdd_rdwr_duration > 0.0) ? hdd_rdwr_bytes / hdd_rdwr_duration : 0.0;
	stress_metrics_set(args, 2, "MB/sec read/write combined rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

	stress_metrics_set(args, 3, "max extents per file",
		(double)max_extents, STRESS_METRIC_GEOMETRIC_MEAN);

	free(alloc_buf);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_hdd_bytes,      "hdd-bytes",      TYPE_ID_UINT64_BYTES_FS, MIN_HDD_BYTES, MAX_HDD_BYTES, NULL },
	{ OPT_hdd_opts,       "hdd-opts",       TYPE_ID_CALLBACK, 0, 0, stress_hdd_opts },
	{ OPT_hdd_write_size, "hdd-write-size", TYPE_ID_UINT64_BYTES_FS, MIN_HDD_WRITE_SIZE, MAX_HDD_WRITE_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_hdd_info = {
	.stressor = stress_hdd,
	.classifier = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
