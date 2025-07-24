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
#include "core-mmap.h"
#include "core-prime.h"

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#define MIN_DENTRIES		(1)
#define MAX_DENTRIES		(1000000)
#define DEFAULT_DENTRIES	(2048)

#define ORDER_FORWARD		(0x00)
#define ORDER_REVERSE		(0x01)
#define ORDER_STRIDE		(0x02)
#define ORDER_RANDOM		(0x03)
#define ORDER_NONE		(0x04)

typedef struct {
	const char *name;
	const uint8_t denty_order;
} stress_dentry_removal_t;

static const stress_help_t help[] = {
	{ "D N","dentry N",	  "start N dentry thrashing stressors" },
	{ NULL,	"dentry-ops N",	  "stop after N dentry bogo operations" },
	{ NULL,	"dentry-order O", "specify unlink order (reverse, forward, stride)" },
	{ NULL,	"dentries N",	  "create N dentries per iteration" },
	{ NULL,	NULL,		  NULL }
};

static const stress_dentry_removal_t dentry_removals[] = {
	{ "forward",	ORDER_FORWARD },
	{ "reverse",	ORDER_REVERSE },
	{ "stride",	ORDER_STRIDE },
	{ "random",	ORDER_RANDOM },
};

static const char *stress_dentry_order(const size_t i)
{
	return (i < SIZEOF_ARRAY(dentry_removals)) ? dentry_removals[i].name : NULL;
}

/*
 *  stress_dentry_unlink_file()
 *	unlink a file. if verify mode is enabled, read and check
 *	contents to make sure it matches the expected gray code
 */
static void stress_dentry_unlink_file(
	stress_args_t *args,
	const uint64_t gray_code,
	const bool verify,
	uint64_t *read_errors)
{
	char path[PATH_MAX];

	stress_temp_filename_args(args, path, sizeof(path), gray_code * 2);
	if (verify) {
		int fd;
		uint64_t val;

		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			ssize_t rret;

			rret = read(fd, &val, sizeof(val));
			if ((rret == sizeof(val)) && (val != gray_code)) {
				pr_inf("err: %" PRIx64 " vs %" PRIx64 "\n",
					val, gray_code);
				(*read_errors)++;
			}
			(void)close(fd);
		}
	}
	(void)shim_unlink(path);
}

/*
 *  stress_dentry_unlink()
 *	remove all dentries
 */
static int stress_dentry_unlink(
	stress_args_t *args,
	const uint64_t n,
	const uint8_t dentry_order,
	const bool verify)
{
	uint64_t i, j;
	uint64_t prime;
	uint64_t read_errors = 0ULL;
	const uint8_t ord = (dentry_order == ORDER_RANDOM) ?
				stress_mwc8modn(3) : dentry_order;

	switch (ord) {
	case ORDER_REVERSE:
		for (i = 0; i < n; i++) {
			uint64_t gray_code;

			j = (n - 1) - i;
			gray_code = (j >> 1) ^ j;
			stress_dentry_unlink_file(args, gray_code, verify, &read_errors);
		}
		break;
	case ORDER_STRIDE:
		prime = stress_get_next_prime64(n);
		for (i = 0, j = prime; i < n; i++, j += prime) {
			const uint64_t k = j % n;
			const uint64_t gray_code = (k >> 1) ^ k;

			stress_dentry_unlink_file(args, gray_code, verify, &read_errors);
		}
		break;
	case ORDER_FORWARD:
	default:
		for (i = 0; i < n; i++) {
			const uint64_t gray_code = (i >> 1) ^ i;

			stress_dentry_unlink_file(args, gray_code, verify, &read_errors);
		}
		break;
	}

	if (read_errors > 0) {
		pr_fail("%s: %" PRIu64 " files did not contain the expected graycode check data\n",
			args->name, read_errors);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_dentry_state()
 *	determined the number of cached dentries
 */
static void stress_dentry_state(int64_t *nr_dentry)
{
#if defined(__linux__)
	FILE *fp;
	int n;

	fp = fopen("/proc/sys/fs/dentry-state", "r");
	if (!fp)
		goto err;
	n = fscanf(fp, "%" SCNd64, nr_dentry);
	(void)fclose(fp);

	if (n != 1)
		goto err;
	return;
err:
#endif
	*nr_dentry = 0ULL;
	return;
}

/*
 *  stress_dentry_misc()
 *	misc ways to exercise a directory file
 */
static void stress_dentry_misc(const char *path)
{
	int fd, flags = O_RDONLY;
	struct stat statbuf;
#if defined(HAVE_UTIME_H)
	struct utimbuf utim;
#endif
	char buf[1024];
	void *ptr;

#if defined(O_DIRECTORY)
	flags |= O_DIRECTORY;
#endif
	fd = open(path, flags);
	if (fd < 0)
		return;

#if defined(HAVE_UTIME_H)
	(void)utime(path, NULL);
	(void)shim_memset(&utim, 0, sizeof(utim));
	(void)utime(path, &utim);
#endif

	VOID_RET(int, shim_fstat(fd, &statbuf));

	/* Not really legal */
	VOID_RET(off_t, lseek(fd, 0, SEEK_END));

	VOID_RET(off_t, lseek(fd, 0, SEEK_SET));

	/* Not allowed */
	VOID_RET(ssize_t, read(fd, buf, sizeof(buf)));

	/* Not allowed */
	VOID_RET(int, ftruncate(fd, 0));

	/* Not allowed */
	VOID_RET(int, shim_fallocate(fd, 0, (off_t)0, statbuf.st_size));

	/* mmap */
	ptr = stress_mmap_populate(NULL, 4096, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);

#if defined(HAVE_FUTIMENS) &&	\
    defined(UTIME_NOW)
	{
		struct timespec ts[2];

		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;

		VOID_RET(int, futimens(fd, &ts[0]));
	}
#endif

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
	{
		struct timeval timeout;
		fd_set rdfds;

		FD_ZERO(&rdfds);
		FD_SET(fd, &rdfds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		VOID_RET(int, select(fd + 1, &rdfds, NULL, NULL, &timeout));
	}
#endif

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
	/*
	 *  flock capable systems..
	 */
	{
		int ret;

		ret = flock(fd, LOCK_EX);
		if (ret == 0) {
			VOID_RET(int, flock(fd, LOCK_UN));
		}
	}
#elif defined(F_SETLKW) &&	\
      defined(F_RDLCK) &&	\
      defined(F_UNLCK)
	/*
	 *  ..otherwise fall back to fcntl (e.g. Solaris)
	 */
	{
		struct flock lock;
		int ret;

		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_type = F_RDLCK;
		ret = fcntl(fd, F_SETLKW, &lock);
		if (ret == 0) {
			lock.l_start = 0;
			lock.l_len = 0;
			lock.l_whence = SEEK_SET;
			lock.l_type = F_UNLCK;
			VOID_RET(int, fcntl(fd, F_SETLKW, &lock));
		}
	}
#endif

#if defined(F_GETFL)
	{
		int flag;

		VOID_RET(int, fcntl(fd, F_GETFL, &flag));
	}
#endif
	(void)close(fd);

}

/*
 *  stress_dentry
 *	stress dentries.  file names are based
 *	on a gray-coded value multiplied by two.
 *	Even numbered files exist, odd don't exist.
 */
static int stress_dentry(stress_args_t *args)
{
	int ret, rc = EXIT_SUCCESS;
	uint64_t dentries = DEFAULT_DENTRIES;
	uint64_t dentry_offset = dentries;
	uint8_t dentry_order = ORDER_RANDOM;
	char dir_path[PATH_MAX];
	int64_t nr_dentry1, nr_dentry2, nr_dentries;
	double creat_duration = 0.0, creat_count = 0.0;
	double access_duration = 0.0, access_count = 0.0;
	double bogus_access_duration = 0.0, bogus_access_count = 0.0;
	double bogus_unlink_duration = 0.0, bogus_unlink_count = 0.0;
	double rate;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (!stress_get_setting("dentries", &dentries)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			dentries = MAX_DENTRIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			dentries = MIN_DENTRIES;
	}
	(void)stress_get_setting("dentry-order", &dentry_order);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_dir(dir_path, sizeof(dir_path), args->name,
		args->pid, args->instance);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_dentry_state(&nr_dentry1);
	do {
		uint64_t i, n = dentries;
		char path[PATH_MAX];

		for (i = 0; i < n; i++) {
			const uint64_t gray_code = (i >> 1) ^ i;
			int fd;
			double t;

			if (UNLIKELY(!stress_continue(args)))
				goto abort;

			stress_temp_filename_args(args,
				path, sizeof(path), gray_code * 2);

			t = stress_time_now();
			if ((fd = open(path, O_CREAT | O_RDWR,
					S_IRUSR | S_IWUSR)) < 0) {
				if (errno != ENOSPC) {
					pr_fail("%s open %s failed, errno=%d (%s)\n",
						args->name, path, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
				n = i;
				break;
			}
			creat_duration += stress_time_now() - t;
			creat_count += 1.0;

			if (verify) {
				ssize_t wret;

				wret = write(fd, &gray_code, sizeof(gray_code));
				if (wret < 0) {
					(void)close(fd);
					break;
				}
			}
			(void)close(fd);
			stress_bogo_inc(args);
		}

		stress_dentry_misc(dir_path);
		shim_sync();

		/*
		 *  Now look up some bogus names to exercise
		 *  lookup failures
		 */
		for (i = 0; i < n; i++) {
			const uint64_t gray_code = (i >> 1) ^ i;
			double t;

			if (UNLIKELY(!stress_continue(args)))
				goto abort;

			/* The following should succeed */
			stress_temp_filename_args(args,
				path, sizeof(path), gray_code * 2);

			t = stress_time_now();
			if (access(path, R_OK) == 0) {
				access_duration += stress_time_now() - t;
				access_count += 1.0;
			}

			stress_temp_filename_args(args,
				path, sizeof(path), dentry_offset + (gray_code * 2) + 1);
			/* The following should fail */
			t = stress_time_now();
			if (access(path, R_OK) != 0) {
				bogus_access_duration += stress_time_now() - t;
				bogus_access_count += 1.0;
			}

			stress_temp_filename_args(args,
				path, sizeof(path), dentry_offset + i);
			/* The following should fail */
			t = stress_time_now();
			if (access(path, R_OK) != 0) {
				bogus_access_duration += stress_time_now() - t;
				bogus_access_count += 1.0;
			}

			/* The following should fail */
			if (shim_unlink(path) < 0) {
				bogus_unlink_duration += stress_time_now() - t;
				bogus_unlink_count += 1.0;
			}
		}
		dentry_offset += dentries;

		/*
		 *  And remove
		 */
		if (stress_dentry_unlink(args, n, dentry_order, verify) != EXIT_SUCCESS)
			rc = EXIT_FAILURE;

		stress_dentry_misc(dir_path);

		if (UNLIKELY(!stress_continue_flag()))
			break;
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

abort:
	stress_dentry_state(&nr_dentry2);
	nr_dentries = nr_dentry2 - nr_dentry1;
	if (stress_instance_zero(args) && (nr_dentries > 0)) {
		pr_inf("%s: %" PRId64 " dentries allocated\n",
			args->name, nr_dentries);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (creat_count > 0.0) ? (double)creat_duration / creat_count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per file creation",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (access_count > 0.0) ? (double)access_duration / access_count : 0.0;
	stress_metrics_set(args, 1, "nanosecs per file access",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (bogus_access_count > 0.0) ? (double)bogus_access_duration / bogus_access_count : 0.0;
	stress_metrics_set(args, 2, "nanosecs per bogus file access",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (bogus_unlink_count > 0.0) ? (double)bogus_unlink_duration / bogus_unlink_count : 0.0;
	stress_metrics_set(args, 3, "nanosecs per bogus file unlink",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	/* force unlink of all files */
	stress_dentry_unlink(args, dentries, dentry_order, verify);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_dentries,	    "dentries",     TYPE_ID_UINT64, MIN_DENTRIES, MAX_DENTRIES, NULL },
	{ OPT_dentry_order, "dentry-order", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_dentry_order },
	END_OPT,
};

const stressor_info_t stress_dentry_info = {
	.stressor = stress_dentry,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
