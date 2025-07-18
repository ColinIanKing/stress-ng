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
#include "core-mincore.h"

/*
 * stress_mincore_touch_pages_slow()
 *	touch pages, even when they are resident
 */
static void stress_mincore_touch_pages_slow(
	void *buf,
	const size_t n_pages,
	const size_t page_size,
	const bool interruptible)
{
	size_t i;
	volatile char *buffer;

	if (interruptible) {
		for (buffer = buf, i = 0;
		     LIKELY(stress_continue_flag() && (i < n_pages));
		     i++, buffer += page_size) {
			(*buffer)++;
		}
		for (buffer = buf, i = 0;
		     LIKELY(stress_continue_flag() && (i < n_pages));
		     i++, buffer += page_size) {
			(*buffer)--;
		}
	} else {
		for (buffer = buf, i = 0; i < n_pages; i++, buffer += page_size) {
			(*buffer)++;
		}
		for (buffer = buf, i = 0; i < n_pages; i++, buffer += page_size) {
			(*buffer)--;
		}
	}
}

/*
 * stress_mincore_touch_pages_generic()
 *	touch a range of pages, ensure they are all in memory,
 *	can be interrupted if interruptible is true
 */
static int stress_mincore_touch_pages_generic(
	void *buf,
	const size_t buf_len,
	const bool interruptible)
{
	const size_t page_size = stress_get_page_size();
	const size_t n_pages = buf_len / page_size;

#if !defined(HAVE_MINCORE)
	if (!(g_opt_flags & OPT_FLAGS_MMAP_MINCORE))
		return 0;
	/* systems that don't have mincore */
	stress_mincore_touch_pages_slow(buf, n_pages, page_size, interruptible);
	return 0;
#else
	/* systems that support mincore */
	unsigned char *vec;
	volatile char *buffer;
	size_t i;
	const uintptr_t uintptr = (uintptr_t)buf & (page_size - 1);

	if (!(g_opt_flags & OPT_FLAGS_MMAP_MINCORE))
		return 0;
	if (n_pages < 1)
		return -1;

	vec = (unsigned char *)calloc(n_pages, 1);
	if (!vec) {
		stress_mincore_touch_pages_slow(buf, n_pages, page_size, interruptible);
		return 0;
	}

	/*
	 *  Find range of pages that are not in memory
	 */
	if (shim_mincore((void *)uintptr, buf_len, vec) < 0) {
		free(vec);

		stress_mincore_touch_pages_slow(buf, n_pages, page_size, interruptible);
		return 0;
	}

	if (interruptible) {
		/* If page is not resident in memory, touch it */
		for (buffer = buf, i = 0;
		     LIKELY(stress_continue_flag() && (i < n_pages));
		     i++, buffer += page_size) {
			if (!(vec[i] & 1))
				(*buffer)++;
		}

		/* And restore contents */
		for (buffer = buf, i = 0;
		     LIKELY(stress_continue_flag() && (i < n_pages));
		     i++, buffer += page_size) {
			if (!(vec[i] & 1))
				(*buffer)--;
		}
	} else {
		/* If page is not resident in memory, touch it */
		for (buffer = buf, i = 0; i < n_pages; i++, buffer += page_size) {
			if (!(vec[i] & 1))
				(*buffer)++;
		}

		/* And restore contents */
		for (buffer = buf, i = 0; i < n_pages; i++, buffer += page_size) {
			if (!(vec[i] & 1))
				(*buffer)--;
		}
	}
	free(vec);
	return 0;
#endif
}

/*
 *  stress_mincore_touch_pages()
 *	touch a range of pages, ensure they are all in memory, non-interruptible
 */
int stress_mincore_touch_pages(void *buf, const size_t buf_len)
{
#if defined(MADV_POPULATE_READ) &&	\
    defined(MADV_POPULATE_WRITE) &&	\
    defined(HAVE_MADVISE)
	int ret;

	if (!(g_opt_flags & OPT_FLAGS_MMAP_MINCORE))
		return 0;
	ret = madvise(buf, buf_len, MADV_POPULATE_READ);
	if (ret == 0) {
		ret = madvise(buf, buf_len, MADV_POPULATE_WRITE);
		if (ret == 0)
			return 0;
	}
#endif
	return stress_mincore_touch_pages_generic(buf, buf_len, false);
}

/*
 *  stress_mincore_touch_pages()
 *	touch a range of pages, ensure they are all in memory, interruptible
 */
int stress_mincore_touch_pages_interruptible(void *buf, const size_t buf_len)
{
	return stress_mincore_touch_pages_generic(buf, buf_len, true);
}
