/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyrignt (C) 2021-2024 Colin Ian King.
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
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-put.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif
#if defined(HAVE_SYS_SENDFILE_H)
#include <sys/sendfile.h>
#endif

#define MIN_IOMIX_BYTES		(1 * MB)
#define MAX_IOMIX_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_IOMIX_BYTES	(1 * GB)

typedef void (*stress_iomix_func)(stress_args_t *args, const int fd, const char *fs_type, const off_t iomix_bytes);

static const stress_help_t help[] = {
	{ NULL,	"iomix N",	 "start N workers that have a mix of I/O operations" },
	{ NULL,	"iomix-bytes N", "write N bytes per iomix worker (default is 1GB)" },
	{ NULL,	"iomix-ops N",	 "stop iomix workers after N iomix bogo operations" },
	{ NULL, NULL,		 NULL }
};

static void *counter_lock;

/*
 *  stress_iomix_rnd_offset()
 *	generate a random offset between 0..max-1
 */
static inline off_t stress_iomix_rnd_offset(const off_t max)
{
	return (off_t)stress_mwc64modn((uint64_t)max);
}

/*
 *  stress_iomix_fadvise_random_dontneed()
 *	hint that the data at offset is not needed
 *	and the I/O is random for more stress
 */
static void stress_iomix_fadvise_random_dontneed(
	const int fd,
	const off_t offset,
	const off_t len)
{
#if defined(HAVE_POSIX_FADVISE) &&	\
    (defined(POSIX_FADV_RANDOM) ||	\
     defined(POSIX_FADV_DONTNEED))
	int flag = 0;
#if defined(POSIX_FADV_RANDOM)
	flag |= POSIX_FADV_RANDOM;
#endif
#if defined(POSIX_FADV_DONTNEED)
	flag |= POSIX_FADV_DONTNEED;
#endif
	(void)posix_fadvise(fd, offset, len, flag);
#else
	(void)fd;
	(void)offset;
	(void)len;
#endif
}

/*
 *  stress_iomix_fsync_min_1Hz()
 *	sync written data at most every once a second while
 *	trying to minimize the number time get calls
 */
static void stress_iomix_fsync_min_1Hz(const int fd)
{
	static double time_last = -1.0;
	static int counter = 0;
	static int counter_max = 1;

	if (time_last <= 0.0)
		time_last = stress_time_now() + 1.0;

	if (UNLIKELY(counter++ >= counter_max)) {
		const double now = stress_time_now();
		const double delta = now - time_last;

		/* Less than 1Hz? try again */
		if (delta < 1.0)
			return;

		counter_max = (int)((double)counter / delta);

		counter = 0;
		time_last = now;

		switch (stress_mwc8modn(3)) {
		case 0:
			(void)shim_fsync(fd);
			break;
		case 1:
			(void)shim_fdatasync(fd);
			break;
		case 2:
			shim_sync();
			break;
		}
	}
}

/*
 *  stress_iomix_wr_seq_bursts()
 *	bursty sequential writes
 */
static void stress_iomix_wr_seq_bursts(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn;
		const int n = stress_mwc8();
		int i;

		posn = stress_iomix_rnd_offset(iomix_bytes);
		ret = lseek(fd, posn, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}
#if defined(HAVE_POSIX_FADVISE) &&      \
    defined(POSIX_FADV_SEQUENTIAL)
		if (posn < iomix_bytes)
			(void)posix_fadvise(fd, posn, iomix_bytes - posn, POSIX_FADV_SEQUENTIAL);
#else
		UNEXPECTED
#endif
		for (i = 0; (i < n) && (posn < iomix_bytes); i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));

			stress_rndbuf(buffer, len);

			rc = write(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
				}
			}
			posn += rc;

			if (!stress_bogo_inc_lock(args, counter_lock, true))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
		(void)shim_usleep(stress_mwc32modn(1000000));
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_wr_rnd_bursts()
 *	bursty random writes
 */
static void stress_iomix_wr_rnd_bursts(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
#if defined(HAVE_POSIX_FADVISE) &&      \
    defined(POSIX_FADV_RANDOM)
	(void)posix_fadvise(fd, 0, iomix_bytes, POSIX_FADV_RANDOM);
#else
	UNEXPECTED
#endif
	do {
		const int n = stress_mwc8();
		int i;

		for (i = 0; i < n; i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));
			off_t ret, posn;

			posn = stress_iomix_rnd_offset(iomix_bytes);
			ret = lseek(fd, posn, SEEK_SET);
			if (UNLIKELY(ret == (off_t)-1)) {
				if (errno == EINTR)
					return;
				pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}

			stress_rndbuf(buffer, len);
			rc = write(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
				}
			}
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
		(void)shim_usleep(stress_mwc32modn(2000000));
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_wr_seq_slow()
 *	slow sequential writes
 */
static void stress_iomix_wr_seq_slow(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}
#if defined(HAVE_POSIX_FADVISE) &&      \
    defined(POSIX_FADV_SEQUENTIAL)
		if (posn < iomix_bytes)
			(void)posix_fadvise(fd, posn, iomix_bytes - posn, POSIX_FADV_SEQUENTIAL);
#else
		UNEXPECTED
#endif
		while (posn < iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));

			stress_rndbuf(buffer, len);

			rc = write(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
				}
			}
			(void)shim_usleep(250000);
			posn += rc;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_rd_seq_bursts()
 *	bursty sequential reads
 */
static void stress_iomix_rd_seq_bursts(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn;
		const int n = stress_mwc8();
		int i;

		posn = stress_iomix_rnd_offset(iomix_bytes);
		ret = lseek(fd, posn, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}

#if defined(HAVE_POSIX_FADVISE) &&      \
    defined(POSIX_FADV_SEQUENTIAL)
		if (posn < iomix_bytes)
			(void)posix_fadvise(fd, posn, iomix_bytes - posn, POSIX_FADV_SEQUENTIAL);
#else
		UNEXPECTED
#endif
		for (i = 0; (i < n) && (posn < iomix_bytes); i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));

			rc = read(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				pr_fail("%s: read failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}
			posn += rc;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;

			/* Add some unhelpful advice */
			stress_iomix_fadvise_random_dontneed(fd, posn, 4096);
		}
		(void)shim_usleep(stress_mwc32modn(1000000));
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_rd_rnd_bursts()
 *	bursty random reads
 */
static void stress_iomix_rd_rnd_bursts(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		const int n = stress_mwc8();
		int i;

		for (i = 0; i < n; i++) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));
			off_t ret, posn;

			posn = stress_iomix_rnd_offset(iomix_bytes);

			/* Add some unhelpful advice */
			stress_iomix_fadvise_random_dontneed(fd, posn, (ssize_t)len);

			ret = lseek(fd, posn, SEEK_SET);
			if (UNLIKELY(ret == (off_t)-1)) {
				if (errno == EINTR)
					return;
				pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}

			rc = read(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				pr_fail("%s: read failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
		}
		(void)shim_usleep(3000000);
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_rd_seq_slow()
 *	slow sequential reads
 */
static void stress_iomix_rd_seq_slow(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}
#if defined(HAVE_POSIX_FADVISE) &&      \
    defined(POSIX_FADV_SEQUENTIAL)
		if (posn < iomix_bytes)
			(void)posix_fadvise(fd, posn, iomix_bytes - posn, POSIX_FADV_SEQUENTIAL);
#else
		UNEXPECTED
#endif
		while (posn < iomix_bytes) {
			char buffer[512];
			ssize_t rc;
			const size_t len = 1 + (stress_mwc32() & (sizeof(buffer) - 1));

			/* Add some unhelpful advice */
			stress_iomix_fadvise_random_dontneed(fd, posn, (ssize_t)len);

			rc = read(fd, buffer, len);
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				pr_fail("%s: read failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}
			(void)shim_usleep(333333);
			posn += rc;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_sync()
 *	file syncs
 */
static void stress_iomix_sync(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;

	do {
		(void)shim_fsync(fd);
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
			break;
		(void)shim_usleep(stress_mwc32modn(4000000));
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;

#if defined(HAVE_FDATASYNC)
		(void)shim_fdatasync(fd);
		/* Exercise illegal fdatasync */
		(void)shim_fdatasync(-1);
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;
		(void)shim_usleep(stress_mwc32modn(4000000));
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
			break;
#else
		UNEXPECTED
#endif
#if defined(HAVE_SYNC_FILE_RANGE) &&	\
    defined(SYNC_FILE_RANGE_WRITE)
		{
			const off_t posn = stress_iomix_rnd_offset(iomix_bytes);

			(void)sync_file_range(fd, posn, 65536,
				SYNC_FILE_RANGE_WRITE);
			stress_iomix_fadvise_random_dontneed(fd, posn, 65536);

			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
				break;
			(void)shim_usleep(stress_mwc32modn(4000000));
		}
#else
		(void)iomix_bytes;
		UNEXPECTED
#endif
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_DONTNEED)
/*
 *  stress_iomix_bad_advise()
 *	bad fadvise hints
 */
static void stress_iomix_bad_advise(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;

	do {
		off_t posn = stress_iomix_rnd_offset(iomix_bytes);

		(void)posix_fadvise(fd, posn, 65536, POSIX_FADV_DONTNEED);
		(void)shim_usleep(100000);
		(void)posix_fadvise(fd, posn, 65536, POSIX_FADV_NORMAL);
		(void)shim_usleep(100000);
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

/*
 *  stress_iomix_rd_wr_mmap()
 *	random memory mapped read/writes
 */
static void stress_iomix_rd_wr_mmap(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	void *mmaps[128];
	size_t i;
	const size_t page_size = args->page_size;

	(void)fs_type;

	do {
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			const off_t posn = stress_iomix_rnd_offset(iomix_bytes) & ~((off_t)page_size - 1);

			mmaps[i] = stress_mmap_populate(NULL, page_size, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, fd, posn);
		}
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			if (LIKELY(mmaps[i] != MAP_FAILED)) {
				const uint8_t *buffer = (uint8_t *)mmaps[i];

				/* Force page data to be read */
				stress_uint8_put(buffer[0]);

				stress_rndbuf(mmaps[i], page_size);
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
				(void)shim_msync(mmaps[i], page_size,
					stress_mwc1() ? MS_ASYNC : MS_SYNC);
#endif
			}
		}
		(void)shim_usleep(100000);
		for (i = 0; i < SIZEOF_ARRAY(mmaps); i++) {
			if (LIKELY(mmaps[i] != MAP_FAILED))
				(void)munmap(mmaps[i], page_size);
		}
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}

/*
 *  stress_iomix_wr_bytes()
 *	lots of small 1 byte writes
 */
static void stress_iomix_wr_bytes(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = 0;

		ret = lseek(fd, 0, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}
		while (posn < iomix_bytes) {
			char buffer[1] = { stress_mwc8modn(26) + 'A' };
			ssize_t rc;

			rc = write(fd, buffer, sizeof(buffer));
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
			}	}
			(void)shim_usleep(1000);
			posn += rc;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_wr_rev_bytes()
 *	lots of small 1 byte writes in reverse order
 */
static void stress_iomix_wr_rev_bytes(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t ret, posn = iomix_bytes;

		ret = lseek(fd, 0, SEEK_SET);
		if (UNLIKELY(ret == (off_t)-1)) {
			if (errno == EINTR)
				return;
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			return;
		}
		while (posn != 0) {
			char buffer[1] = { stress_mwc8modn(26) + 'A' };
			ssize_t rc;

			rc = write(fd, buffer, sizeof(buffer));
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
			}	}
			(void)shim_usleep(1000);
			posn--;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
			stress_iomix_fsync_min_1Hz(fd);
		}
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}

/*
 *  stress_iomix_rd_bytes()
 *	lots of small 1 byte reads
 */
static void stress_iomix_rd_bytes(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	do {
		off_t posn = iomix_bytes;

		while (posn != 0) {
			char buffer[1];
			ssize_t rc;
			off_t ret;

			/* Add some unhelpful advice */
			stress_iomix_fadvise_random_dontneed(fd, posn, sizeof(buffer));

			ret = lseek(fd, posn, SEEK_SET);
			if (UNLIKELY(ret == (off_t)-1)) {
				if (errno == EINTR)
					return;
				pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				return;
			}

			rc = read(fd, buffer, sizeof(buffer));
			if (UNLIKELY(rc < 0)) {
				if (errno == EINTR)
					break;
				if ((errno != EPERM) && (errno != ENOSPC)) {
					pr_fail("%s: write failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno), fs_type);
					return;
				}
			}
			(void)shim_usleep(1000);
			posn--;
			if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
				return;
		}
	} while (stress_bogo_inc_lock(args, counter_lock, false));
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
	stress_args_t *args,
	const int fd,
	const int flag,
	bool *ok)
{
#if defined(FS_IOC_GETFLAGS)
	int ret, attr;

	if (UNLIKELY(!stress_continue(args)))
		return;

	ret = ioctl(fd, FS_IOC_GETFLAGS, &attr);
	if (UNLIKELY(ret < 0))
		return;
#if defined(FS_IOC_SETFLAGS)
	attr |= flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	if (UNLIKELY(ret < 0))
		return;

	attr &= ~flag;
	ret = ioctl(fd, FS_IOC_SETFLAGS, &attr);
	if (UNLIKELY(ret < 0))
		return;
#else
	UNEXPECTED
#endif
#else
	UNEXPECTED
#endif
	*ok = true;
}
#endif

/*
 *  stress_iomix_inode_flags()
 *	twiddle various inode flags
 */
static void stress_iomix_inode_flags(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)args;
	(void)fd;
	(void)fs_type;
	(void)iomix_bytes;

	do {
		bool ok = false;
#if defined(FS_APPEND_FL)
		stress_iomix_inode_ioctl(args, fd, FS_APPEND_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_COMPR_FL)
		stress_iomix_inode_ioctl(args, fd, FS_COMPR_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_IMMUTABLE_FL)
		stress_iomix_inode_ioctl(args, fd, FS_IMMUTABLE_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_JOURNAL_DATA_FL)
		stress_iomix_inode_ioctl(args, fd, FS_JOURNAL_DATA_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_NOATIME_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOATIME_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_NOCOW_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOCOW_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_NODUMP_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NODUMP_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_NOTAIL_FL)
		stress_iomix_inode_ioctl(args, fd, FS_NOTAIL_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_SECRM_FL)
		stress_iomix_inode_ioctl(args, fd, FS_SECRM_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_SYNC_FL)
		stress_iomix_inode_ioctl(args, fd, FS_SYNC_FL, &ok);
#else
		UNEXPECTED
#endif
#if defined(FS_UNRM_FL)
		stress_iomix_inode_ioctl(args, fd, FS_UNRM_FL, &ok);
#else
		UNEXPECTED
#endif
		if (!ok)
			_exit(EXIT_SUCCESS);
		stress_iomix_fsync_min_1Hz(fd);
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

#if defined(__linux__)
/*
 *  stress_iomix_drop_caches()
 *	occasional file cache dropping
 */
static void stress_iomix_drop_caches(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fd;
	(void)fs_type;
	(void)iomix_bytes;

	do {
		shim_sync();
		if (stress_system_write("/proc/sys/vm/drop_caches", "1", 1) < 0)
			(void)shim_pause();
		(void)sleep(5);
		if (UNLIKELY(!stress_continue(args)))
			return;
		shim_sync();
		if (stress_system_write("/proc/sys/vm/drop_caches", "2", 1) < 0)
			(void)shim_pause();
		(void)sleep(5);
		if (UNLIKELY(!stress_continue(args)))
			return;
		shim_sync();
		if (stress_system_write("/proc/sys/vm/drop_caches", "3", 1) < 0)
			(void)shim_pause();
		(void)sleep(5);
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

#if defined(HAVE_COPY_FILE_RANGE)
/*
 *  stress_iomix_copy_file_range()
 *	lots of copies with copy_file_range
 */
static void stress_iomix_copy_file_range(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;

	do {
		shim_off64_t from = (shim_off64_t)stress_iomix_rnd_offset(iomix_bytes);
		shim_off64_t to = (shim_off64_t)stress_iomix_rnd_offset(iomix_bytes);
		const size_t size = stress_mwc16();

		VOID_RET(ssize_t, shim_copy_file_range(fd, &from, fd, &to, size, 0));
		VOID_RET(ssize_t, shim_copy_file_range(fd, &to, fd, &from, size, 0));

		if (UNLIKELY(!stress_continue(args)))
			return;
		stress_iomix_fsync_min_1Hz(fd);

		(void)shim_usleep(stress_mwc32modn(100000));
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE)
/*
 *  stress_iomix_sendfile()
 *	lots of copies with copy_file_range
 */
static void stress_iomix_sendfile(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;

	do {
		off_t from = stress_iomix_rnd_offset(iomix_bytes);
		off_t to = stress_iomix_rnd_offset(iomix_bytes);
		off_t ret;
		const size_t size = stress_mwc16();

		ret = lseek(fd, to, SEEK_SET);
		if (UNLIKELY(ret != (off_t)-1)) {
			ssize_t sret;

			sret = sendfile(fd, fd, &from, size);
			(void)sret;
		}

		if (UNLIKELY(!stress_continue(args)))
			return;
		stress_iomix_fsync_min_1Hz(fd);

		(void)shim_usleep(stress_mwc32modn(130000));
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

#if defined(__linux__) &&	\
    defined(__NR_cachestat)
struct shim_cachestat_range {
	uint64_t off;
	uint64_t len;
};

struct shim_cachestat {
	uint64_t nr_cache;
	uint64_t nr_dirty;
	uint64_t nr_writeback;
	uint64_t nr_evicted;
	uint64_t nr_recently_evicted;
};

/*
 *  shim_cachestat
 *	wrapper for cachestat system call
 */
static inline int shim_cachestat(
	int fd,
	struct shim_cachestat_range *cstat_range,
	struct shim_cachestat *cstat,
	unsigned int flags)
{
	return (int)syscall(__NR_cachestat,
			(unsigned long int)fd,
			(unsigned long int)cstat_range,
			(unsigned long int)cstat,
			(unsigned long int)flags);
}

/*
 *  stress_iomix_cachestat()
 *	various periodic cache statistics calls (linux only)
 */
static void stress_iomix_cachestat(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;
	(void)iomix_bytes;

	do {
		struct stat buf;

		if (shim_fstat(fd, &buf) == 0) {
			struct shim_cachestat_range cstat_range;
			struct shim_cachestat cstat;

			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)buf.st_size;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, 0));

			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)512;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, 0));

			cstat_range.off = (uint64_t)buf.st_size;
			cstat_range.len = (uint64_t)512;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, 0));

			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)0ULL;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, 0));

			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)iomix_bytes;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, 0));

			/* exercise invalid flags */
			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)buf.st_size;
			VOID_RET(int, shim_cachestat(fd, &cstat_range, &cstat, ~0));

			/* exercise invalid fd */
			cstat_range.off = (uint64_t)0ULL;
			cstat_range.len = (uint64_t)buf.st_size;
			VOID_RET(int, shim_cachestat(100000, &cstat_range, &cstat, 0));
		}
		(void)shim_usleep(50000);
	} while (stress_bogo_inc_lock(args, counter_lock, true));
}
#endif

#if defined(HAVE_READAHEAD)
static void stress_iomix_readahead(
	stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t iomix_bytes)
{
	(void)fs_type;

	do {
		const off_t offset = stress_iomix_rnd_offset(iomix_bytes);
		const size_t len = 512 * stress_mwc8modn(16);

		VOID_RET(int, readahead(fd, offset, len));

		(void)shim_usleep(stress_mwc32modn(2000000));
		if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
			return;
	} while (stress_bogo_inc_lock(args, counter_lock, false));
}
#endif

static const stress_iomix_func iomix_funcs[] = {
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
	stress_iomix_wr_rev_bytes,
	stress_iomix_rd_bytes,
#if defined(__linux__)
	stress_iomix_inode_flags,
#endif
#if defined(__linux__)
	stress_iomix_drop_caches,
#endif
#if defined(HAVE_COPY_FILE_RANGE)
	stress_iomix_copy_file_range,
#endif
#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE)
	stress_iomix_sendfile,
#endif
#if defined(__linux__) &&	\
    defined(__NR_cachestat)
	stress_iomix_cachestat,
#endif
#if defined(HAVE_READAHEAD)
	stress_iomix_readahead,
#endif
};

#define MAX_IOMIX_PROCS	(SIZEOF_ARRAY(iomix_funcs))

/*
 *  stress_iomix
 *	stress I/O via random mix of io ops
 */
static int stress_iomix(stress_args_t *args)
{
	int fd, ret;
	char filename[PATH_MAX];
	off_t iomix_bytes, iomix_bytes_total;
	uint64_t iomix_bytes_u64 = DEFAULT_IOMIX_BYTES;
	const size_t page_size = args->page_size;
	size_t i;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	const char *fs_type;
	int oflags = O_CREAT | O_RDWR;
	bool iomix_bytes_shrunk = false;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_sync_s_pids_mmap(MAX_IOMIX_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
			args->name, MAX_IOMIX_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

#if defined(O_SYNC)
	oflags |= O_SYNC;
#endif

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		ret = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}

	if (!stress_get_setting("iomix-bytes", &iomix_bytes_u64)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			iomix_bytes_u64 = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			iomix_bytes_u64 = MIN_IOMIX_BYTES;
	}
	iomix_bytes_total = (off_t)iomix_bytes_u64;
	iomix_bytes = iomix_bytes_total / args->instances;
	if (iomix_bytes < (off_t)MIN_IOMIX_BYTES) {
		iomix_bytes = (off_t)MIN_IOMIX_BYTES;
		iomix_bytes_total = iomix_bytes * args->instance;
	}
	if (iomix_bytes < (off_t)page_size) {
		iomix_bytes = (off_t)page_size;
		iomix_bytes_total = iomix_bytes * args->instance;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, iomix_bytes, iomix_bytes_total);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = stress_exit_status(-ret);
		goto lock_destroy;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, oflags, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto lock_destroy;
	}
	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	do {
#if defined(FALLOC_FL_ZERO_RANGE)
		ret = shim_fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, iomix_bytes);
#else
		ret = shim_fallocate(fd, 0, 0, iomix_bytes);
#endif
		if (UNLIKELY(ret < 0)) {
			switch (errno) {
			case EFBIG:
			case ENOSPC:
				if (iomix_bytes > (off_t)MIN_IOMIX_BYTES) {
					iomix_bytes >>= 1;
					iomix_bytes_shrunk = true;
				} else {
					pr_fail("%s: fallocate failed, no free space, errno=%d (%s)%s, skipping stressor\n",
						args->name, errno, strerror(errno), fs_type);
					ret = EXIT_NO_RESOURCE;
					goto tidy;
				}
				break;
			default:
				pr_fail("%s: fallocate failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				ret = EXIT_FAILURE;
				goto tidy;
			}
		}
	} while ((ret < 0) && stress_continue(args));

	if (iomix_bytes_shrunk)
		pr_inf("%s: file size too large for file system, reducing file size to %" PRIdMAX " MB\n",
			args->name, (intmax_t)iomix_bytes >> 20);

	stress_file_rw_hint_short(fd);

	for (i = 0; i < MAX_IOMIX_PROCS; i++) {
		stress_sync_start_init(&s_pids[i]);

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			goto reap;
		} else if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			/* Child */
			(void)sched_settings_apply(true);
			iomix_funcs[i](args, fd, fs_type, iomix_bytes);
			_exit(EXIT_SUCCESS);
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)shim_pause();
	} while (stress_bogo_inc_lock(args, counter_lock, false));

	ret = EXIT_SUCCESS;
reap:
	stress_kill_and_wait_many(args, s_pids, MAX_IOMIX_PROCS, SIGALRM, true);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
lock_destroy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_lock_destroy(counter_lock);
tidy_s_pids:
	(void)stress_sync_s_pids_munmap(s_pids, MAX_IOMIX_PROCS);

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_iomix_bytes, "iomix-bytes", TYPE_ID_UINT64_BYTES_FS, MIN_IOMIX_BYTES, MAX_IOMIX_BYTES, NULL },
	END_OPT,
};

const stressor_info_t stress_iomix_info = {
	.stressor = stress_iomix,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
