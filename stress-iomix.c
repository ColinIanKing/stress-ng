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

typedef void (*stress_iomix_func)(const args_t *args, const int fd, const off_t iomix_bytes);

static const help_t help[] = {
	{ NULL,	"iomix N",	 "start N workers that have a mix of I/O operations" },
	{ NULL,	"iomix-bytes N", "write N bytes per iomix worker (default is 1GB)" },
	{ NULL,	"iomix-ops N",	 "stop iomix workers after N iomix bogo operations" },
	{ NULL, NULL,		 NULL }
};

static int stress_set_iomix_bytes(const char *opt)
{
	off_t iomix_bytes;

	iomix_bytes = (off_t)get_uint64_byte_filesystem(opt, 1);
	check_range_bytes("iomix-bytes", iomix_bytes,
		MIN_IOMIX_BYTES, MAX_IOMIX_BYTES);
	return set_setting("iomix-bytes", TYPE_ID_OFF_T, &iomix_bytes);
}

/*
 *  stress_iomix_wr_seq_bursts()
 *	bursty sequential writes
 */
static void stress_iomix_wr_seq_bursts(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn;
		const int n = mwc8();
		int i;
		struct timeval tv;

		posn = mwc64() % iomix_bytes;
		ret = lseek(fd, posn, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		for (i = 0; (i < n) && (posn < iomix_bytes); i++) {
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
			if (!keep_stressing())
				return;
			inc_counter(args);
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
static void stress_iomix_wr_rnd_bursts(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
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

			posn = mwc64() % iomix_bytes;
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
			if (!keep_stressing())
				return;
			inc_counter(args);
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
static void stress_iomix_wr_seq_slow(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

			stress_strnrnd(buffer, len);

			rc = write(fd, buffer, len);
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			(void)shim_usleep(250000);
			posn += rc;
			if (!keep_stressing())
				return;
			inc_counter(args);
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_seq_bursts()
 *	bursty sequential reads
 */
static void stress_iomix_rd_seq_bursts(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn;
		const int n = mwc8();
		int i;
		struct timeval tv;

		posn = mwc64() % iomix_bytes;
		ret = lseek(fd, posn, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
#if defined(HAVE_POSIX_FADVISE)
		(void)posix_fadvise(fd, posn, 1024 * 1024, POSIX_FADV_SEQUENTIAL);
#endif
		for (i = 0; (i < n) && (posn < iomix_bytes); i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

			rc = read(fd, buffer, len);
			if (rc < 0) {
				pr_fail("read");
				return;
			}
			posn += rc;
			if (!keep_stressing())
				return;
			inc_counter(args);
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
static void stress_iomix_rd_rnd_bursts(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
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

			posn = mwc64() % iomix_bytes;
#if defined(HAVE_POSIX_FADVISE)
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
			if (!keep_stressing())
				return;
			inc_counter(args);
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
static void stress_iomix_rd_seq_slow(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (mwc32() & (sizeof(buffer) - 1));

#if defined(HAVE_POSIX_FADVISE)
			(void)posix_fadvise(fd, posn, len, POSIX_FADV_SEQUENTIAL);
#endif
			rc = read(fd, buffer, len);
			if (rc < 0) {
				pr_fail("read");
				return;
			}
			(void)shim_usleep(333333);
			posn += rc;
			if (!keep_stressing())
				return;
			inc_counter(args);
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_sync()
 *	file syncs
 */
static void stress_iomix_sync(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		struct timeval tv;

		(void)shim_fsync(fd);
		if (!keep_stressing())
			break;
		inc_counter(args);
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
		if (!keep_stressing())
			break;

#if defined(HAVE_FDATASYNC)
		(void)shim_fdatasync(fd);
		if (!keep_stressing())
			break;
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
		if (!keep_stressing())
			break;
#endif
#if defined(HAVE_SYNC_FILE_RANGE) &&	\
    defined(SYNC_FILE_RANGE_WRITE)
		(void)sync_file_range(fd, mwc64() % iomix_bytes, 65536, SYNC_FILE_RANGE_WRITE);
		if (!keep_stressing())
			break;
		tv.tv_sec = mwc32() % 4;
		tv.tv_usec = mwc32() % 1000000;
		(void)select(0, NULL, NULL, NULL, &tv);
#else
		(void)iomix_bytes;
#endif
	} while (keep_stressing());
}

#if defined(HAVE_POSIX_FADVISE)
/*
 *  stress_iomix_bad_advise()
 *	bad fadvise hints
 */
static void stress_iomix_bad_advise(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t posn = mwc64() % iomix_bytes;

		(void)posix_fadvise(fd, posn, 65536, POSIX_FADV_DONTNEED);
		(void)shim_usleep(100000);
	} while (keep_stressing());
}
#endif

/*
 *  stress_iomix_rd_wr_mmap()
 *	random memory mapped read/writes
 */
static void stress_iomix_rd_wr_mmap(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
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
			off_t posn = (mwc64() % iomix_bytes) & ~(page_size - 1);
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
					mwc1() ? MS_ASYNC : MS_SYNC);
			}
		}
		(void)shim_usleep(100000);
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			if (mmaps[i] != MAP_FAILED)
				(void)munmap(mmaps[i], page_size);
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_wr_bytes()
 *	lots of small 1 byte writes
 */
static void stress_iomix_wr_bytes(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (ret < 0) {
			pr_fail("seek");
			return;
		}
		while (posn < iomix_bytes) {
			char buffer[1] = { (mwc8() % 26) + 'A' };
			ssize_t rc;

			rc = write(fd, buffer, sizeof(buffer));
			if (rc < 0) {
				pr_fail("write");
				return;
			}
			(void)shim_usleep(1000);
			posn += rc;
			if (!keep_stressing())
				return;
			inc_counter(args);
		}
	} while (keep_stressing());
}

/*
 *  stress_iomix_rd_bytes()
 *	lots of small 1 byte reads
 */
static void stress_iomix_rd_bytes(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = iomix_bytes;

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
			(void)shim_usleep(1000);
			posn--;
			if (!keep_stressing())
				return;
			inc_counter(args);
		}
	} while (keep_stressing());
}

#if defined(__linux__)

#if defined(FS_APPEND_FL)	|| \
    defined(FS_COMPR_FL)	|| \
    defined(FS_IMMUTABLE_FL)	|| \
    defined(FS_JOURNAL_DATA_FL)	|| \
    defined(FS_NOATIME_FL)	|| \
    defined(FS_NOCOW_FL)	|| \
    defined(FS_NODUMP_FL)	|| \
    defined(FS_NOTAIL_FL)	|| \
    defined(FS_SECRM_FL)	|| \
    defined(FS_SYNC_FL)		|| \
    defined(FS_UNRM_FL)
/*
 *  stress_iomix_inode_ioctl()
 *	attempt to set and unset a file based inode flag
 */
static void stress_iomix_inode_ioctl(
	const args_t *args,
	const int fd,
	const int flag,
	bool *ok)
{
	int ret, attr;

	if (!keep_stressing())
		return;

#if defined(FS_IOC_GETFLAGS)
	ret = ioctl(fd, FS_IOC_GETFLAGS, &attr);
	if (ret < 0)
		return;
#if defined(FS_IOC_SETFLAGS)
	attr |= flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	if (ret < 0)
		return;

	attr &= ~flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	if (ret < 0)
		return;
#endif
#endif
	*ok = true;
}
#endif

/*
 *  stress_iomix_inode_flags()
 *	twiddle various inode flags
 */
static void stress_iomix_inode_flags(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	(void)args;
	(void)iomix_bytes;

	do {
		bool ok = false;
#if defined(FS_APPEND_FL)
		stress_iomix_inode_ioctl(args, fd, FS_APPEND_FL, &ok);
#endif
#if defined(FS_COMPR_FL)
		stress_iomix_inode_ioctl(args, fd, FS_COMPR_FL, &ok);
#endif
#if defined(FS_IMMUTABLE_FL)
		stress_iomix_inode_ioctl(args, fd, FS_IMMUTABLE_FL, &ok);
#endif
#if defined(FS_JOURNAL_DATA_FL)
		stress_iomix_inode_ioctl(args, fd, FS_JOURNAL_DATA_FL, &ok);
#endif
#if defined(FS_NOATIME_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOATIME_FL, &ok);
#endif
#if defined(FS_NOCOW_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOCOW_FL, &ok);
#endif
#if defined(FS_NODUMP_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NODUMP_FL, &ok);
#endif
#if defined(FS_NOTAIL_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOTAIL_FL, &ok);
#endif
#if defined(FS_SECRM_FL)
		stress_iomix_inode_ioctl(args, fd, FS_SECRM_FL, &ok);
#endif
#if defined(FS_SYNC_FL)
		stress_iomix_inode_ioctl(args, fd, FS_SYNC_FL, &ok);
#endif
#if defined(FS_UNRM_FL)
		stress_iomix_inode_ioctl(args, fd, FS_UNRM_FL, &ok);
#endif
		if (!ok)
			_exit(EXIT_SUCCESS);
	} while (keep_stressing());
}
#endif

#if defined(__linux__)
/*
 *  stress_iomix_drop_caches()
 *	occasional file cache dropping
 */
static void stress_iomix_drop_caches(
	const args_t *args,
	const int fd,
	const off_t iomix_bytes)
{
	(void)fd;
	(void)iomix_bytes;

	do {
		(void)sync();
		if (system_write("/proc/sys/vm/drop_caches", "1", 1) < 0)
			(void)pause();
		(void)sleep(5);
		if (!keep_stressing())
			return;
		(void)sync();
		if (system_write("/proc/sys/vm/drop_caches", "2", 1) < 0)
			(void)pause();
		(void)sleep(5);
		if (!keep_stressing())
			return;
		(void)sync();
		if (system_write("/proc/sys/vm/drop_caches", "3", 1) < 0)
			(void)pause();
		(void)sleep(5);
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
#if defined(HAVE_POSIX_FADVISE)
	stress_iomix_bad_advise,
#endif
	stress_iomix_rd_wr_mmap,
	stress_iomix_wr_bytes,
	stress_iomix_rd_bytes,
#if defined(__linux__)
	stress_iomix_inode_flags,
#endif
#if defined(__linux__)
	stress_iomix_drop_caches
#endif
};

/*
 *  stress_iomix
 *	stress I/O via random mix of io ops
 */
static int stress_iomix(const args_t *args)
{
	int fd, ret;
	char filename[PATH_MAX];
	uint64_t *counters;
	off_t iomix_bytes = DEFAULT_IOMIX_BYTES;
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

	if (!get_setting("iomix-bytes", &iomix_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			iomix_bytes = MAX_FALLOCATE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			iomix_bytes = MIN_FALLOCATE_BYTES;
	}
	iomix_bytes /= args->num_instances;
	if (iomix_bytes < (off_t)MIN_IOMIX_BYTES)
		iomix_bytes = (off_t)MIN_IOMIX_BYTES;
	if (iomix_bytes < (off_t)page_size)
		iomix_bytes = page_size;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = exit_status(-ret);
		goto unmap;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR | O_SYNC, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err("open");
		goto unmap;
	}
	(void)unlink(filename);

#if defined(FALLOC_FL_ZERO_RANGE)
	ret = shim_fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, iomix_bytes);
#else
	ret = shim_fallocate(fd, 0, 0, iomix_bytes);
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

	(void)memset(pids, 0, sizeof(pids));
	(void)memset(counters, 0, sz);

	for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
		const args_t tmp_args = {
			.counter = &counters[i],
			.name = args->name,
			.max_ops = args->max_ops,
			.instance = args->instance,
			.num_instances = args->num_instances,
			.pid = args->pid,
			.ppid = args->ppid,
			.page_size = args->page_size
		};

		pids[i] = fork();
		if (pids[i] < 0) {
			goto reap;
		} else if (pids[i] == 0) {
			/* Child */
			iomix_funcs[i](&tmp_args, fd, iomix_bytes);
			_exit(EXIT_SUCCESS);
		}
	}

	do {
		uint64_t c = 0;
		(void)shim_usleep(5000);
		for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
			c += counters[i];
			if (UNLIKELY(args->max_ops && c >= args->max_ops)) {
				set_counter(args, c);
				goto reap;
			}
		}
	} while (keep_stressing());

	ret = EXIT_SUCCESS;
reap:
	set_counter(args, 0);
	for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
		add_counter(args, counters[i]);

		if (pids[i]) {
			(void)kill(pids[i], SIGALRM);
			(void)kill(pids[i], SIGKILL);
		}
	}
	for (i = 0; i < SIZEOF_ARRAY(iomix_funcs); i++) {
		if (pids[i]) {
			int status;

			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

tidy:
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
unmap:
	(void)munmap((void *)counters, sz);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_iomix_bytes,	stress_set_iomix_bytes },
	{ 0,			NULL }
};

stressor_info_t stress_iomix_info = {
	.stressor = stress_iomix,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
