/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

#define BUF_ALIGNMENT		(4096)

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

static uint64_t opt_hdd_bytes = DEFAULT_HDD_BYTES;
static uint64_t opt_hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
static int opt_hdd_flags = 0;
static int opt_hdd_oflags = 0;

typedef struct {
	const char *opt;	/* User option */
	int flag;		/* HDD_OPT_ flag */
	int exclude;		/* Excluded HDD_OPT_ flags */
	int advice;		/* posix_fadvise value */
	int oflag;		/* open O_* flags */
} hdd_opts_t;

static const hdd_opts_t hdd_opts[] = {
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
#if defined(POSIX_FADV_NORMAL) && !defined(__gnu_hurd__)
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
#if defined(POSIX_FADV_SEQ) && !defined(__gnu_hurd__)
	{ "fadv-seq",	HDD_OPT_FADV_SEQ,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_RND),
		POSIX_FADV_SEQUENTIAL, 0 },
#endif
#if defined(POSIX_FADV_RND) && !defined(__gnu_hurd__)
	{ "fadv-rnd",	HDD_OPT_FADV_RND,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_SEQ),
		POSIX_FADV_RANDOM, 0 },
#endif
#if defined(POSIX_FADV_NOREUSE) && !defined(__gnu_hurd__)
	{ "fadv-noreuse", HDD_OPT_FADV_NOREUSE,
		HDD_OPT_FADV_NORMAL,
		POSIX_FADV_NOREUSE, 0 },
#endif
#if defined(POSIX_FADV_WILLNEED) && !defined(__gnu_hurd__)
	{ "fadv-willneed", HDD_OPT_FADV_WILLNEED,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_DONTNEED),
		POSIX_FADV_WILLNEED, 0 },
#endif
#if defined(POSIX_FADV_DONTNEED) && !defined(__gnu_hurd__)
	{ "fadv-dontneed", HDD_OPT_FADV_DONTNEED,
		(HDD_OPT_FADV_NORMAL | HDD_OPT_FADV_WILLNEED),
		POSIX_FADV_DONTNEED, 0 },
#endif
	{ NULL, 0, 0, 0, 0 }
};

void stress_set_hdd_bytes(const char *optarg)
{
	opt_hdd_bytes =  get_uint64_byte(optarg);
	check_range("hdd-bytes", opt_hdd_bytes,
		MIN_HDD_BYTES, MAX_HDD_BYTES);
}

void stress_set_hdd_write_size(const char *optarg)
{
	opt_hdd_write_size = get_uint64_byte(optarg);
	check_range("hdd-write-size", opt_hdd_write_size,
		MIN_HDD_WRITE_SIZE, MAX_HDD_WRITE_SIZE);
}

/*
 *  stress_hdd_opts
 *	parse --hdd-opts option(s) list
 */
int stress_hdd_opts(char *opts)
{
	char *str, *token, *saveptr = NULL;

	for (str = opts; (token = strtok_r(str, ",", &saveptr)) != NULL; str = NULL) {
		int i;
		bool opt_ok = false;

		for (i = 0; hdd_opts[i].opt; i++) {
			if (!strcmp(token, hdd_opts[i].opt)) {
				int exclude = opt_hdd_flags & hdd_opts[i].exclude;
				if (exclude) {
					int j;

					for (j = 0; hdd_opts[j].opt; j++) {
						if ((exclude & hdd_opts[j].flag) == exclude) {
							fprintf(stderr,
								"hdd-opt option '%s' is not "
								"compatible with option '%s'\n",
								token,
								hdd_opts[j].opt);
							break;
						}
					}
					return -1;
				}
				opt_hdd_flags  |= hdd_opts[i].flag;
				opt_hdd_oflags |= hdd_opts[i].oflag;
				opt_ok = true;
			}
		}
		if (!opt_ok) {
			fprintf(stderr, "hdd-opt option '%s' not known, options are:", token);
			for (i = 0; hdd_opts[i].opt; i++)
				fprintf(stderr, "%s %s",
					i == 0 ? "" : ",", hdd_opts[i].opt);
			fprintf(stderr, "\n");
			return -1;
		}
	}

	return 0;
}

/*
 *  stress_hdd_advise()
 *	set posix_fadvise options
 */
static int stress_hdd_advise(const char *name, const int fd, const int flags)
{
#if (defined(POSIX_FADV_SEQ) || defined(POSIX_FADV_RND) || \
    defined(POSIX_FADV_NOREUSE) || defined(POSIX_FADV_WILLNEED) || \
    defined(POSIX_FADV_DONTNEED)) && !defined(__gnu_hurd__)
	int i;

	if (!(flags & HDD_OPT_FADV_MASK))
		return 0;

	for (i = 0; hdd_opts[i].opt; i++) {
		if (hdd_opts[i].flag & flags) {
			if (posix_fadvise(fd, 0, 0, hdd_opts[i].advice) < 0) {
				pr_failed_err(name, "posix_fadvise");
				return -1;
			}
		}
	}
#else
	(void)name;
	(void)fd;
	(void)flags;
#endif
	return 0;
}
/*
 *  stress_hdd
 *	stress I/O via writes
 */
int stress_hdd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	uint64_t i;
	const pid_t pid = getpid();
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	int flags = O_CREAT | O_RDWR | O_TRUNC | opt_hdd_oflags;
	int fadvise_flags = opt_hdd_flags & HDD_OPT_FADV_MASK;

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	/* Must have some write option */
	if ((opt_hdd_flags & HDD_OPT_WR_MASK) == 0)
		opt_hdd_flags |= HDD_OPT_WR_SEQ;

	ret = posix_memalign((void **)&buf, BUF_ALIGNMENT, (size_t)opt_hdd_write_size);
	if (ret || !buf) {
		pr_err(stderr, "%s: cannot allocate buffer\n", name);
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}

	for (i = 0; i < opt_hdd_write_size; i++)
		buf[i] = (uint8_t)mwc();

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());
	do {
		int fd;

		(void)umask(0077);
		if ((fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
			pr_failed_err(name, "open");
			goto finish;
		}
		(void)unlink(filename);

		if (stress_hdd_advise(name, fd, fadvise_flags) < 0)
			goto finish;

		/* Sequential Write */
		if (opt_hdd_flags & HDD_OPT_WR_SEQ) {
			for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
				if (write(fd, buf, (size_t)opt_hdd_write_size) < 0) {
					pr_failed_err(name, "write");
					(void)close(fd);
					goto finish;
				}
				(*counter)++;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;
			}
		}
		/* Random Write */
		if (opt_hdd_flags & HDD_OPT_WR_RND) {
			for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
				off_t offset = (mwc() % opt_hdd_bytes) & ~511;
				if (lseek(fd, offset, SEEK_SET) < 0) {
					pr_failed_err(name, "lseek");
					(void)close(fd);
					goto finish;
				}
				if (write(fd, buf, (size_t)opt_hdd_write_size) < 0) {
					pr_failed_err(name, "write");
					(void)close(fd);
					goto finish;
				}
				(*counter)++;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;
			}
		}
		/* Sequential Read */
		if (opt_hdd_flags & HDD_OPT_RD_SEQ) {
			uint64_t misreads = 0;

			if (lseek(fd, 0, SEEK_SET) < 0) {
				pr_failed_err(name, "lseek");
				(void)close(fd);
				goto finish;
			}
			for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
				ssize_t ret;

				ret = read(fd, buf, (size_t)opt_hdd_write_size);
				if (ret < 0) {
					pr_failed_err(name, "read");
					(void)close(fd);
					goto finish;
				}
				if (ret != (ssize_t)opt_hdd_write_size)
					misreads++;

				(*counter)++;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;
			}
			pr_dbg(stderr, "%s: %" PRIu64 " incomplete reads\n",
				name, misreads);
		}
		/* Random Read */
		if (opt_hdd_flags & HDD_OPT_RD_RND) {
			uint64_t misreads = 0;

			for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
				ssize_t ret;
				off_t offset = (mwc() % opt_hdd_bytes) & ~511;

				if (lseek(fd, offset, SEEK_SET) < 0) {
					pr_failed_err(name, "lseek");
					(void)close(fd);
					goto finish;
				}
				ret = read(fd, buf, (size_t)opt_hdd_write_size);
				if (ret < 0) {
					pr_failed_err(name, "read");
					(void)close(fd);
					goto finish;
				}
				if (ret != (ssize_t)opt_hdd_write_size)
					misreads++;

				(*counter)++;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;
			}
			pr_dbg(stderr, "%s: %" PRIu64 " incomplete reads\n",
				name, misreads);
		}
		(void)close(fd);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
finish:
	free(buf);
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}
