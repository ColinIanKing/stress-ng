// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
