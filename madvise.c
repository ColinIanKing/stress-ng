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

#if !defined(__gnu_hurd__) && !defined(__minix__)
static const int madvise_options[] = {
#if defined(MADV_NORMAL)
	MADV_NORMAL,
#endif
#if defined(MADV_RANDOM)
	MADV_RANDOM,
#endif
#if defined(MADV_SEQUENTIAL)
	MADV_SEQUENTIAL,
#endif
#if defined(MADV_WILLNEED)
	MADV_WILLNEED,
#endif
/*
 *  Don't use DONTNEED as this can zero fill
 *  pages that don't have backing store which
 *  trips checksum errors when we check that
 *  the pages are sane.
 *
#if defined(MADV_DONTNEED)
	MADV_DONTNEED,
#endif
*/
#if defined(MADV_DONTFORK)
	MADV_DONTFORK,
#endif
#if defined(MADV_DOFORK)
	MADV_DOFORK,
#endif
#if defined(MADV_MERGEABLE)
	MADV_MERGEABLE,
#endif
#if defined(MADV_UNMERGEABLE)
	MADV_UNMERGEABLE,
#endif
#if defined(MADV_HUGEPAGE)
	MADV_HUGEPAGE,
#endif
#if defined(MADV_NOHUGEPAGE)
	MADV_NOHUGEPAGE,
#endif
#if defined(MADV_DONTDUMP)
	MADV_DONTDUMP,
#endif
#if defined(MADV_DODUMP)
	MADV_DODUMP,
#endif
/*
 *  Don't use MADV_FREE as this can zero fill
 *  pages that don't have backing store which
 *  trips checksum errors when we check that
 *  the pages are sane.
 *
#if defined(MADV_FREE)
	MADV_FREE
#endif
*/
};
#endif

/*
 * madvise_random()
 *	apply random madvise setting to a memory region
 */
int madvise_random(void *addr, const size_t length)
{
#if !defined(__gnu_hurd__) && !defined(__minix__)
	if (g_opt_flags & OPT_FLAGS_MMAP_MADVISE) {
		int i = (mwc32() >> 7) % SIZEOF_ARRAY(madvise_options);

		return madvise(addr, length, madvise_options[i]);
	}
#else
	(void)addr;
	(void)length;
#endif
	return 0;
}
