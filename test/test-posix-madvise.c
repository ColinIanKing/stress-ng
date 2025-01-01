/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
