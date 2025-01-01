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
#include "core-mlock.h"

/*
 *  stress_mlock_region
 *	mlock a region of memory so it can't be swapped out
 *	- used to lock sighandlers for faster response
 */
int stress_mlock_region(const void *addr_start, const void *addr_end)
{
#if defined(HAVE_MLOCK)
	const size_t page_size = stress_get_page_size();
	const void *m_addr_start =
		(void *)((uintptr_t)addr_start & ~(page_size - 1));
	const void *m_addr_end =
		(void *)(((uintptr_t)addr_end + page_size - 1) &
		~(page_size - 1));
	const size_t len = (uintptr_t)m_addr_end - (uintptr_t)m_addr_start;

	return shim_mlock((const void *)m_addr_start, len);
#else
	UNEXPECTED
	(void)addr_start;
	(void)addr_end;

	return 0;
#endif
}
