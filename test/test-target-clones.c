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

#include <features.h>
#include "../core-version.h"

/*
 *  For now, only x86-64 systems with GNUC > 5.5 are known
 *  to support this attribute reliably.
 */
#if (defined(__GNUC__) &&	\
     defined(__GLIBC__) &&	\
     NEED_GNUC(5,5,0)) ||	\
    (defined(__clang__) &&	\
     NEED_CLANG(14,0,0))

#if defined(__x86_64__) ||	\
    defined(__x86_64) ||	\
    defined(__amd64__) ||	\
    defined(__amd64) || 	\
    defined(__PPC64__)
#else
#error arch not supported
#endif

#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONE)))

int TARGET_CLONES have_target_clones(void)
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
