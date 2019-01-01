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

#include <features.h>
#include "../stress-version.h"

/*
 *  For now, only x86-64 systems with GNUC > 5.5 are known
 *  to support this attribute reliably.
 */
#if defined(__GNUC__) && \
    defined(__GLIBC__) && \
    NEED_GNUC(5,5,0) && \
    (defined(__x86_64__) || defined(__x86_64))

#define TARGET_CLONES	__attribute__((target_clones("sse","sse2","ssse3", "sse4.1", "sse4a", "avx","avx2","default")))

static int TARGET_CLONES have_target_clones(void)
{
	return 0;
}

int main(void)
{
	return have_target_clones();
}

#else
#error target clones attribute not supported
#endif
