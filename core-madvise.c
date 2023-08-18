// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_MADVISE)
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
#if defined(MADV_COLD)
	MADV_COLD,
#endif
#if defined(MADV_PAGEOUT)
	MADV_PAGEOUT,
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
/* Linux 5.14 */
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
};
#endif

/*
 *  stress_madvise_random()
 *	apply random madvise setting to a memory region
 */
int stress_madvise_random(void *addr, const size_t length)
{
#if defined(HAVE_MADVISE)
	if (g_opt_flags & OPT_FLAGS_MMAP_MADVISE) {
		const int i = stress_mwc32modn((uint32_t)SIZEOF_ARRAY(madvise_options));

		return madvise(addr, length, madvise_options[i]);
	}
#else
	UNEXPECTED
	(void)addr;
	(void)length;
#endif
	return 0;
}

/*
 *  stress_madvise_pid_all_pages()
 *	apply madvise advise to all pages in a progress
 */
void stress_madvise_pid_all_pages(const pid_t pid, const int advise)
{
#if defined(HAVE_MADVISE) &&	\
    defined(__linux__)
	FILE *fp;
	char path[PATH_MAX];
	char buf[4096];

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/maps", (intmax_t)pid);

	fp = fopen(path, "r");
	if (!fp)
		return;
	while (fgets(buf, sizeof(buf), fp)) {
		void *start, *end, *offset;
		int major, minor, n;
		uint64_t inode;
		char prot[5];

		n = sscanf(buf, "%p-%p %4s %p %x:%x %" PRIu64 " %s\n",
			&start, &end, prot, &offset, &major, &minor,
			&inode, path);
		if (n < 7)
			continue;	/* bad sscanf data */
		if (start >= end)
			continue;	/* invalid address range */

		VOID_RET(int, madvise(start, (size_t)((uint8_t *)end - (uint8_t *)start), advise));

		/*
		 *  Readable protection? read pages
		 */
		if ((prot[0] == 'r') && (path[0] != '[')) {
			const size_t page_size = stress_get_page_size();

			volatile uint8_t *ptr = (volatile uint8_t *)start;

			while (ptr < (uint8_t *)end) {
				(*ptr);
				ptr += page_size;
			}
		}
	}

	(void)fclose(fp);
#else
	(void)pid;
	(void)advise;
#endif
}
