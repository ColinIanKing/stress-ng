// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/mman.h>

static char buffer[8192];

static const int posix_madvise_options[] = {
#if defined(POSIX_MADV_NORMAL)
	POSIX_MADV_NORMAL,
#endif
#if defined(POSIX_MADV_RANDOM)
	POSIX_MADV_RANDOM,
#endif
#if defined(POSIX_MADV_SEQUENTIAL)
	POSIX_MADV_SEQUENTIAL,
#endif
#if defined(POSIX_MADV_WILLNEED)
	POSIX_MADV_WILLNEED,
#endif
#if defined(POSIX_MADV_DONTNEED)
	POSIX_MADV_DONTNEED,
#endif
};

/*
 *  The following enum will cause test build failure
 *  if there are no madvise options
 */
enum {
	NO_POSIX_MADVISE_OPTIONS = 1 / sizeof(posix_madvise_options)
};

int main(void)
{
	/* should have at least POSIX_MADV_NORMAL */
	return posix_madvise(buffer, sizeof(buffer), POSIX_MADV_NORMAL);
}
