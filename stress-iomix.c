/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

typedef void (*stress_iomix_func)(const args_t *args, const int fd);

static off_t opt_iomix_bytes = DEFAULT_IOMIX_BYTES;
static bool set_iomix_bytes = false;

void stress_set_iomix_bytes(const char *optarg)
{
	set_iomix_bytes = true;
	opt_iomix_bytes = (off_t)
		get_uint64_byte_filesystem(optarg,
			stressor_instances(STRESS_IOMIX));
	check_range_bytes("iomix-bytes", opt_iomix_bytes,
		MIN_IOMIX_BYTES, MAX_IOMIX_BYTES);
}

/*
 *  stress_iomix_wr_seq_bursts()
 *	bursty sequential writes
 */
static void stress_iomix_wr_seq_bursts(const args_t *args, const int fd)
{
	do {
		off_t ret, posn;
		const int n = mwc8();
		int i;
		struct timeval tv;

		posn = mwc64() % opt_iomix_bytes;
		ret = lseek(fd, posn, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		for (i = 0; (i < n) && (posn < opt_iomix_bytes); i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

			stress_strnrnd(buffer, len);

			rc = write(fd, buffer, len);
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			posn += rc;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
		tv.tv_sec = 0;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
	} while (keep_stressing());
}

/*
 *  stress_iomix_wr_rnd_bursts()
 *	bursty random writes
 */
static void stress_iomix_wr_rnd_bursts(const args_t *args, const int fd)
{
	do {
		const int n = mwc8();
		int i;
		struct timeval tv;

		for (i = 0; i < n; i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));
			off_t ret, posn;

			posn = mwc64() % opt_iomix_bytes;
			ret = lseek(fd, posn, SEEK_SET);
			if (ret < 0) {
				pr_fail("seek");
				return;
			}

			stress_strnrnd(buffer, len);
			rc = write(fd, buffer, len);
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
		tv.tv_sec = mwc32() % 2;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);

	} while (keep_stressing());
}

/*
 *  stress_iomix_wr_seq_slow()
 *	slow sequential writes
 */
static void stress_iomix_wr_seq_slow(const args_t *args, const int fd)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < opt_iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

			stress_strnrnd(buffer, len);

			rc = write(fd, buffer, len);
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			usleep(250000);
			posn += rc;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_seq_bursts()
 *	bursty sequential reads
 */
static void stress_iomix_rd_seq_bursts(const args_t *args, const int fd)
{
	do {
		off_t ret, posn;
		const int n = mwc8();
		int i;
		struct timeval tv;

		posn = mwc64() % opt_iomix_bytes;
		ret = lseek(fd, posn, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
#if defined(__linux__)
		(void)posix_fadvise(fd, posn, 1024 * 1024, POSIX_FADV_SEQUENTIAL);
#endif
		for (i = 0; (i < n) && (posn < opt_iomix_bytes); i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

			rc = read(fd, buffer, len);
			if (rc < 0) {
				pr_fail("read");
				return;
			}
			posn += rc;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
		tv.tv_sec = 0;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_rnd_bursts()
 *	bursty random reads
 */
static void stress_iomix_rd_rnd_bursts(const args_t *args, const int fd)
{
	do {
		const int n = mwc8();
		int i;
		struct timeval tv;

		for (i = 0; i < n; i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));
			off_t ret, posn;

			posn = mwc64() % opt_iomix_bytes;
#if defined(__linux__)
			(void)posix_fadvise(fd, posn, len, POSIX_FADV_RANDOM);
#endif
			ret = lseek(fd, posn, SEEK_SET);
			if (ret < 0) {
				pr_fail("seek");
				return;
			}

			rc = read(fd, buffer, len);
			if (rc < 0) {
				pr_fail("read");
				return;
			}
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
		tv.tv_sec = mwc32() % 3;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_seq_slow()
 *	slow sequential reads
 */
static void stress_iomix_rd_seq_slow(const args_t *args, const int fd)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < opt_iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

#if defined(__linux__)
			(void)posix_fadvise(fd, posn, len, POSIX_FADV_SEQUENTIAL);
#endif
			rc = read(fd, buffer, len);
			if (rc < 0) {
				pr_fail("read");
				return;
			}
			usleep(333333);
			posn += rc;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_sync()
 *	file syncs
 */
static void stress_iomix_sync(const args_t *args, const int fd)
{
	do {
		struct timeval tv;

		fsync(fd);
		inc_counter(args);
		if (!keep_stressing())
			break;
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
		if (!keep_stressing())
			break;

#if defined(__linux__)
		fdatasync(fd);
		inc_counter(args);
		if (!keep_stressing())
			break;
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
		if (!keep_stressing())
			break;
#endif
#if defined(__linux__)
		(void)sync_file_range(fd, mwc64() % opt_iomix_bytes, 65536, SYNC_FILE_RANGE_WRITE);
		inc_counter(args);
		if (!keep_stressing())
			break;
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
#endif
	} while (keep_stressing());
}

#if defined(__linux__)
/*
 *  stress_iomix_bad_advise()
 *	bad fadvise hints
 */
static void stress_iomix_bad_advise(const args_t *args, const int fd)
{
	do {
		off_t posn = mwc64() % opt_iomix_bytes;

		(void)posix_fadvise(fd, posn, 65536, POSIX_FADV_DONTNEED);
		usleep(100000);
	} while (keep_stressing());
}
#endif

/*
 *  stress_iomix_rd_wr_mmap()
 *	random memory mapped read/writes
 */
static void stress_iomix_rd_wr_mmap(const args_t *args, const int fd)
{
	void *mmaps[128];
	size_t i;
	const size_t page_size = args->page_size;
	int flags = MAP_SHARED | MAP_ANONYMOUS;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	do {
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			off_t posn = (mwc64() % opt_iomix_bytes) & ~(page_size - 1);
			mmaps[i] = mmap(NULL, page_size,
					PROT_READ | PROT_WRITE, flags, fd, posn);
		}
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			if (mmaps[i] != MAP_FAILED) {
				size_t j;
				uint64_t sum = 0;
				uint8_t *buffer =  (uint8_t *)mmaps[i];

				/* Force page data to be read */
				for (j = 0; j < page_size; j++)
					sum += buffer[j];
				uint64_put(sum);

				stress_strnrnd(mmaps[i], page_size);
				(void)shim_msync(mmaps[i], page_size,
					(mwc32() & 1) ? MS_ASYNC : MS_SYNC);
			}
		}
		usleep(100000);
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			if (mmaps[i] != MAP_FAILED)
				munmap(mmaps[i], page_size);
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_wr_bytes()
 *	lots of small 1 byte writes
 */
static void stress_iomix_wr_bytes(const args_t *args, const int fd)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < opt_iomix_bytes) {
			char buffer[1] = { (mwc8() % 26) + 'A' };
			ssize_t rc;

			rc = write(fd, buffer, sizeof(buffer));
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			usleep(1000);
			posn += rc;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_bytes()
 *	lots of small 1 byte reads
 */
static void stress_iomix_rd_bytes(const args_t *args, const int fd)
{
	do {
		off_t ret, posn = opt_iomix_bytes;

		while (posn != 0) {
			char buffer[1];
			ssize_t rc;

			ret = lseek(fd, posn, SEEK_SET);
			if (ret < 0) {
				pr_fail("seek");
				return;
			}

			rc = read(fd, buffer, sizeof(buffer));
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			usleep(1000);
			posn--;
			inc_counter(args);
			if (!keep_stressing())
				return;
		}
	} while (keep_stressing());
}

#if defined(__linux__)
/*
 *  stress_iomix_drop_caches()
 *	occasional file cache dropping
 */
static void stress_iomix_drop_caches(const args_t *args, const int fd)
{
	(void)fd;

	do {
		sync();
		if (system_write("/proc/sys/vm/drop_caches", "1", 1) < 0)
			pause();
		sleep(5);
		if (!keep_stressing())
			return;
		sync();
		if (system_write("/proc/sys/vm/drop_caches", "2", 1) < 0)
			pause();
		sleep(5);
		if (!keep_stressing())
			return;
		sync();
		if (system_write("/proc/sys/vm/drop_caches", "3", 1) < 0)
			pause();
		sleep(5);
	} while (keep_stressing());
}
#endif

static stress_iomix_func iomix_funcs[] = {
	stress_iomix_wr_seq_bursts,
	stress_iomix_wr_rnd_bursts,
	stress_iomix_wr_seq_slow,
	stress_iomix_wr_seq_slow,
	stress_iomix_rd_seq_bursts,
	stress_iomix_rd_rnd_bursts,
	stress_iomix_rd_seq_slow,
	stress_iomix_rd_seq_slow,
	stress_iomix_sync,
#if defined(__linux__)
	stress_iomix_bad_advise,
#endif
	stress_iomix_rd_wr_mmap,
	stress_iomix_wr_bytes,
	stress_iomix_rd_bytes,
#if defined(__linux__)
	stress_iomix_drop_caches
#endif
};

/*
 *  stress_iomix
 *	stress I/O via random mix of io ops
 */
int stress_iomix(const args_t *args)
{
	int fd, ret;
	char filename[PATH_MAX];
	uint64_t *counters;
	const size_t page_size = args->page_size;
	const size_t counters_sz = sizeof(uint64_t) * SIZEOF_ARRAY(iomix_funcs);
	const size_t sz = (counters_sz + page_size) & ~(page_size - 1);
	size_t i;
	int pids[SIZEOF_ARRAY(iomix_funcs)];

	counters = (void *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_fail_dbg("mmap");
		return EXIT_NO_RESOURCE;
	}

	if (!set_iomix_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_iomix_bytes = MAX_FALLOCATE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_iomix_bytes = MIN_FALLOCATE_BYTES;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = exit_status(-ret);
		goto unmap;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR | O_SYNC, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err("open");
		goto unmap;
	}
	(void)unlink(filename);

#if defined(FALLOC_FL_ZERO_RANGE)
	ret = shim_fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, opt_iomix_bytes);
#else
	ret = shim_fallocate(fd, 0, 0, opt_iomix_bytes);
#endif
	if (ret < 0) {
		if (errno == ENOSPC) {
			ret = EXIT_NO_RESOURCE;
		} else {
			ret = EXIT_FAILURE;
			pr_fail_err("fallocate");
		}
		goto tidy;
	}

	memset(pids, 0, sizeof(pids));
	memset(counters, 0, sz);

	for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
		const args_t tmp_args = {
			&counters[i],
			args->name,
			args->max_ops,
			args->instance,
			args->pid,
			args->ppid,
			args->page_size
		};

		pids[i] = fork();
		if (pids[i] < 0) {
			goto reap;
		} else if (pids[i] == 0) {
			/* Child */
			iomix_funcs[i](&tmp_args, fd);
			(void)kill(args->pid, SIGALRM);
			_exit(EXIT_SUCCESS);
		}
	}

	do {
		*args->counter = 0;
		(void)pause();
		for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
			*args->counter += counters[i];
		}
	} while (keep_stressing());

	ret = EXIT_SUCCESS;
reap:
	for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
		if (pids[i]) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)waitpid(pids[i], &status, 0);
		}
	}

tidy:
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
unmap:
	(void)munmap((void *)counters, sz);

	return ret;
}
