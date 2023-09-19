// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <fcntl.h>

#if defined(__gnu_hurd__)
#error posix_fadvise is defined but not implemented and will always fail
#endif

#define NO_FADV

int main(void)
{
	static const char filename[] = "/tmp/test-msync.tmp";
	int fd, ret, err = 1;

	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		return 1;
	(void)unlink(filename);

#if defined(POSIX_FADV_NORMAL)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_NORMAL);
	(void)ret;
#endif
#if defined(POSIX_FADV_SEQUENTIAL)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_SEQUENTIAL);
	(void)ret;
#endif
#if defined(POSIX_FADV_RANDOM)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_RANDOM);
	(void)ret;
#endif
#if defined(POSIX_FADV_NOREUSE)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_NOREUSE);
	(void)ret;
#endif
#if defined(POSIX_FADV_WILLNEED)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_WILLNEED);
	(void)ret;
#endif
#if defined(POSIX_FADV_DONTNEED)
#undef NO_FADV
	ret = posix_fadvise(fd, 0, 1024, POSIX_FADV_DONTNEED);
	(void)ret;
#endif

#if defined(NO_FADV)
#error no POSIX_FADV advice macros defined!
#endif
	(void)close(fd);

	return err;
}
