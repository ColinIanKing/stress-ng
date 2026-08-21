/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-madvise.h"
#include "core-sort.h"

static uint32_t stress_madvise_flags;
static uint8_t stress_madvise_flags_count;

typedef struct stress_madvise_opts_t {
	const int advice;
	const uint32_t flag;
	const char *name;
} stress_madvise_opts_t;

/*
 * madvise options
 */
#if defined(HAVE_MADVISE)
const int madvise_options[] = {
#if defined(MADV_NORMAL)
	MADV_NORMAL,
#else
	0,
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
#if defined(MADV_DONTNEED)
	MADV_DONTNEED,
#endif
#if defined(MADV_REMOVE)
	MADV_REMOVE,
#endif
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
#if defined(MADV_SOFT_OFFLINE)
	MADV_SOFT_OFFLINE,
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
#if defined(MADV_FREE)
	MADV_FREE,
#endif
#if defined(MADV_HWPOISON)
	MADV_HWPOISON,
#endif
#if defined(MADV_WIPEONFORK)
	MADV_WIPEONFORK,
#endif
#if defined(MADV_KEEPONFORK)
	MADV_KEEPONFORK,
#endif
#if defined(MADV_INHERIT_ZERO)
	MADV_INHERIT_ZERO,
#endif
#if defined(MADV_COLD)
	MADV_COLD,
#endif
#if defined(MADV_PAGEOUT)
	MADV_PAGEOUT,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
#if defined(MADV_DONTNEED_LOCKED)
	MADV_DONTNEED_LOCKED,
#endif
/* Linux 6.0 */
#if defined(MADV_COLLAPSE)
	MADV_COLLAPSE,
#endif
/* FreeBSD */
#if defined(MADV_AUTOSYNC)
	MADV_AUTOSYNC,
#endif
/* FreeBSD and DragonFlyBSD */
#if defined(MADV_CORE)
	MADV_CORE,
#endif
/* FreeBSD */
#if defined(MADV_PROTECT)
	MADV_PROTECT,
#endif
/* Linux 6.12 */
#if defined(MADV_GUARD_INSTALL) &&	\
    defined(MADV_NORMAL)
	/* This makes a page non writable, disable it to avoid SIGSEGVs */
	/* MADV_GUARD_INSTALL, */
#endif
#if defined(MADV_GUARD_REMOVE)
	/* Should always fail as MADV_GUARD_INSTALL is not used */
	MADV_GUARD_REMOVE,
#endif
/* OpenBSD */
#if defined(MADV_SPACEAVAIL)
	MADV_SPACEAVAIL,
#endif
/* OS X */
#if defined(MADV_ZERO_WIRED_PAGES)
	MADV_ZERO_WIRED_PAGES,
#endif
/* Solaris */
#if defined(MADV_ACCESS_DEFAULT)
	MADV_ACCESS_DEFAULT,
#endif
/* Solaris */
#if defined(MADV_ACCESS_LWP)
	MADV_ACCESS_LWP,
#endif
/* Solaris */
#if defined(MADV_ACCESS_MANY)
	MADV_ACCESS_MANY,
#endif
/* DragonFlyBSD */
#if defined(MADV_INVAL)
	MADV_INVAL,
#endif
/* DragonFlyBSD */
#if defined(MADV_NOCORE)
	MADV_NOCORE,
#endif
};

const size_t madvise_options_elements = SIZEOF_ARRAY(madvise_options);

#endif

#if defined(HAVE_MADVISE)
static const stress_madvise_opts_t madvise_random_options[] = {
#if defined(MADV_NORMAL)
	{ MADV_NORMAL,           0x00000001, "normal" },
#endif
#if defined(MADV_RANDOM)
	{ MADV_RANDOM,           0x00000002, "random" },
#endif
#if defined(MADV_SEQUENTIAL)
	{ MADV_SEQUENTIAL,       0x00000004, "sequential" },
#endif
#if defined(MADV_WILLNEED)
	{ MADV_WILLNEED,         0x00000008, "willneed" },
#endif
/*
 *  Don't use DONTNEED as this can zero fill
 *  pages that don't have backing store which
 *  trips checksum errors when we check that
 *  the pages are sane.
 *
#if defined(MADV_DONTNEED)
	{ MADV_DONTNEED,         0x00000010, "dontneed" },
#endif
*/
#if defined(MADV_DONTFORK)
	{ MADV_DONTFORK,         0x00000020, "dontfork" },
#endif
#if defined(MADV_DOFORK)
	{ MADV_DOFORK,           0x00000040, "dofork" },
#endif
#if defined(MADV_MERGEABLE)
	{ MADV_MERGEABLE,        0x00000080, "mergeable" },
#endif
#if defined(MADV_UNMERGEABLE)
	{ MADV_UNMERGEABLE,      0x00000100, "unmergeable" },
#endif
#if defined(MADV_HUGEPAGE)
	{ MADV_HUGEPAGE,         0x00000200, "hugepage" },
#endif
#if defined(MADV_NOHUGEPAGE)
	{ MADV_NOHUGEPAGE,       0x00000400, "nohugepage" },
#endif
#if defined(MADV_DONTDUMP)
	{ MADV_DONTDUMP,         0x00000800, "dontdump" },
#endif
#if defined(MADV_DODUMP)
	{ MADV_DODUMP,           0x00001000, "dodump" },
#endif
#if defined(MADV_COLD)
	{ MADV_COLD,             0x00002000, "cold" },
#endif
#if defined(MADV_PAGEOUT)
	{ MADV_PAGEOUT,          0x00004000, "pageout" },
#endif
/*
 *  Don't use MADV_FREE as this can zero fill
 *  pages that don't have backing store which
 *  trips checksum errors when we check that
 *  the pages are sane.
 *
#if defined(MADV_FREE)
	{ MADV_FREE,             0x00008000, "free" },
#endif
*/
/* Linux 5.14 */
#if defined(MADV_POPULATE_READ)
	{ MADV_POPULATE_READ,    0x00010000, "populate-read" },
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_WRITE)
	{ MADV_POPULATE_WRITE,   0x00020000, "populate-write" },
#endif
};
#endif

/*
 *  stress_madvise_opts_show()
 *	show the supported madvise advice options
 */
static void stress_madvise_opts_show(void)
{
#if defined(HAVE_MADVISE)
	size_t i;
	const char *opts[SIZEOF_ARRAY(madvise_random_options)];

	for (i = 0; i < SIZEOF_ARRAY(madvise_random_options); i++)
		opts[i] = madvise_random_options[i].name;

	qsort_bm(opts, SIZEOF_ARRAY(opts), sizeof(char *), stress_sort_cmp_str);

	(void)fprintf(stderr, "supported advice:");
	for (i = 0; i < SIZEOF_ARRAY(madvise_random_options); i++)
		(void)fprintf(stderr, " %s", opts[i]);
	(void)fprintf(stderr, "\n");
#else
	(void)fprintf(stderr, "supported advice: (none)\n");
#endif
}

/*
 *  stress_madvise_opts()
 *  	parse --no-madvise-opts to select madvise options to disable
 */
int stress_madvise_opts(void)
{
	char *opt_str = NULL;
	char *dup_str;
	char *str;
	char *saveptr = NULL;
	char *token;
#if defined(HAVE_MADVISE)
	size_t i;
#endif

	stress_madvise_flags = 0;
	stress_madvise_flags_count = 0;

#if defined(HAVE_MADVISE)
	for (i = 0; i < SIZEOF_ARRAY(madvise_random_options); i++) {
		stress_madvise_flags |= madvise_random_options[i].flag;
		stress_madvise_flags_count++;
	}
#endif

	if (!stress_setting_get("no-madvise-opts", &opt_str))
		return 0;

	if (strcmp("?", opt_str) == 0) {
		stress_madvise_opts_show();
		exit(EXIT_SUCCESS);
	}

	dup_str = stress_const_optdup(opt_str);
	if (!dup_str)
		return -1;

	for (str = dup_str; (token = shim_strtok_r(str, ",", &saveptr)) != NULL; str = NULL) {
		bool found = false;
#if defined(HAVE_MADVISE)
		for (i = 0; i < SIZEOF_ARRAY(madvise_random_options); i++) {
			if (strcmp(token, madvise_random_options[i].name) == 0) {
				stress_madvise_flags &= ~madvise_random_options[i].flag;
				stress_madvise_flags_count--;
				found = true;
			}
		}
#endif
		if (!found) {
			(void)fprintf(stderr, "unsupported --no-madvise-opt advice '%s', ", token);
			stress_madvise_opts_show();
			return -1;
		}
	}
	free(dup_str);

	return 0;
}

/*
 *  stress_madvise_randomize()
 *	apply random madvise setting to a memory region
 */
int stress_madvise_randomize(void *addr, const size_t length)
{
#if defined(HAVE_MADVISE)
	if ((g_opt_flags & OPT_FLAGS_MMAP_MADVISE) && (stress_madvise_flags_count > 0)) {
		uint8_t n = stress_mwc8modn(stress_madvise_flags_count);
		uint32_t mask = 0;
		size_t i;
		uint8_t j;

		/*
		 *  Find randomly chosen nth flag bit that is set
		 */
		for (j = 0; j < sizeof(stress_madvise_flags) * 8; j++) {
			mask = 1U << j;
			if (stress_madvise_flags & mask) {
				if (!n)
					break;
				n--;
			}
		}
		/* should never happen! */
		if (UNLIKELY(mask == 0))
			return 0;

		/* Find advice that matches the mask flag */
		for (i = 0; i < SIZEOF_ARRAY(madvise_random_options); i++) {
			if (madvise_random_options[i].flag & mask) {
				const int advice = stress_advice_check(madvise_random_options[i].advice);

				return madvise(addr, length, advice);
			}
		}
	}
#else
	UNEXPECTED
	(void)addr;
	(void)length;
#endif
	return 0;
}

/*
 *  stress_madvise_mergeable()
 *	apply MADV_MERGEABLE for kernel same page merging
 */
int stress_madvise_mergeable(void *addr, const size_t length)
{
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_MERGEABLE)
	return madvise(addr, length, MADV_MERGEABLE);
#else
	(void)addr;
	(void)length;
	return 0;
#endif
}

/*
 *  stress_madvise_collapse()
 *	where possible collapse mapping into THP
 */
int stress_madvise_collapse(void *addr, size_t length)
{
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_COLLAPSE)
	return madvise(addr, length, MADV_COLLAPSE);
#else
	(void)addr;
	(void)length;
	return 0;
#endif
}

/*
 *  stress_madvise_willneed()
 *	where possible fetch pages early
 */
int stress_madvise_willneed(void *addr, size_t length)
{
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_WILLNEED)
	return madvise(addr, length, MADV_WILLNEED);
#else
	(void)addr;
	(void)length;
	return 0;
#endif
}

/*
 *  stress_madvise_nohugepage()
 *	apply MADV_NOHUGEPAGE to force as many PTEs as possible
 */
int stress_madvise_nohugepage(void *addr, const size_t length)
{
#if defined(HAVE_MADVISE) && \
    defined(MADV_NOHUGEPAGE)
	return madvise(addr, length, MADV_NOHUGEPAGE);
#else
	(void)addr;
	(void)length;
	return 0;
#endif
}

/*
 *  stress_madvise_pid_all_pages()
 *	apply madvise advise to all pages in a progress
 */
void stress_madvise_pid_all_pages(
	const pid_t pid,
	const int *advice,
	const size_t n_advice)
{
#if defined(HAVE_MADVISE) &&	\
    defined(__linux__)
	FILE *fp;
	char path[4096];
	char buf[4096];

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/maps", (intmax_t)pid);

	fp = fopen(path, "r");
	if (!fp)
		return;
	while (fgets(buf, sizeof(buf), fp)) {
		void *start;
		void *end;
		void *offset;
		int n;
		unsigned int major;
		unsigned int minor;
		uint64_t inode;
		const size_t page_size = stress_memory_page_size_get();
		char prot[5];

		n = sscanf(buf, "%p-%p %4s %p %x:%x %" PRIu64 " %4095s\n",
			&start, &end, prot, &offset, &major, &minor,
			&inode, path);
		if (n < 7)
			continue;	/* bad sscanf data */
		if (start >= end)
			continue;	/* invalid address range */

		if (n_advice == 1) {
			VOID_RET(int, madvise(start, (size_t)((uint8_t *)end - (uint8_t *)start), advice[0]));
		} else {
			register uint8_t *ptr;

			for (ptr = (uint8_t *)start; ptr < (uint8_t *)end; ptr += page_size) {
				size_t idx = stress_mwc8modn((uint8_t)n_advice);
				const int new_advice = advice[idx];

				VOID_RET(int, madvise((void *)ptr, page_size, new_advice));
			}
		}

		/*
		 *  Readable protection? read pages
		 */
		if ((prot[0] == 'r') && (path[0] != '[')) {
			register volatile uint8_t *vptr = (volatile uint8_t *)start;

			while (vptr < (uint8_t *)end) {
				(*vptr);
				vptr += page_size;
			}
		}
	}

	(void)fclose(fp);
#else
	(void)pid;
	(void)advice;
	(void)n_advice;
#endif
}
