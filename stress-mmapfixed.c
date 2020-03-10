/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"mmapfixed N",		"start N workers stressing mmap with fixed mappings" },
	{ NULL,	"mmapfixed-ops N",	"stop after N mmapfixed bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if UINTPTR_MAX == MAX_32
#define MMAP_TOP	(0x80000000UL)
#else
#define MMAP_TOP	(0x8000000000000000ULL)
#endif
#define MMAP_BOTTOM	(0x10000)

/*
 *  stress_sigsegv_handler()
 *	older kernels can kill the child when fixed mappings
 *	can't be backed by physical pages. In this case,
 *	force child termination and reap and account this
 *	in the main stressor loop.
 */
static void MLOCKED_TEXT stress_sigsegv_handler(int signum)
{
	(void)signum;

	_exit(0);
}

static int stress_mmapfixed_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	uintptr_t addr = MMAP_TOP;
	int ret;

	(void)context;

	ret = stress_sighandler(args->name, SIGSEGV,
				stress_sigsegv_handler, NULL);
	(void)ret;

	do {
		uint8_t *buf;
		int flags = MAP_FIXED | MAP_ANONYMOUS;
		size_t  sz = page_size * (1 + (stress_mwc8() % 7));

#if defined(MAP_SHARED) && defined(MAP_PRIVATE)
		flags |= stress_mwc1() ? MAP_SHARED : MAP_PRIVATE;
#endif
#if defined(MAP_LOCKED)
		flags |= stress_mwc1() ? MAP_LOCKED : 0;
#endif
#if defined(MAP_NORESERVE)
		flags |= stress_mwc1() ? MAP_NORESERVE : 0;
#endif
#if defined(MAP_POPULATE)
		flags |= stress_mwc1() ? MAP_POPULATE : 0;
#endif
#if defined(MAP_FIXED_NOREPLACE)
		/* 4.17 Linux flag */
		flags &= ~MAP_FIXED;
		flags |= stress_mwc1() ? MAP_FIXED : MAP_FIXED_NOREPLACE;
#endif

		if (!keep_stressing_flag())
			break;
		buf = (uint8_t *)mmap((void *)addr, sz,
			PROT_READ, flags, -1, 0);
		if (buf == MAP_FAILED)
			goto next;

		(void)stress_madvise_random(buf, sz);
#if defined(HAVE_MREMAP) && NEED_GLIBC(2,4,0) && \
    defined(MREMAP_FIXED) && \
    defined(MREMAP_MAYMOVE)
		{
			uint8_t *newbuf;
			const uintptr_t newaddr = addr ^
				((page_size << 3) | (page_size << 4));

			newbuf = mremap(buf, sz, sz,
					MREMAP_FIXED | MREMAP_MAYMOVE,
					(void *)newaddr);
			if (newbuf && (newbuf != MAP_FAILED))
				buf = newbuf;

			(void)stress_madvise_random(buf, sz);
		}
#endif
		(void)munmap((void *)buf, sz);
		inc_counter(args);
next:
		addr >>= 1;
		if (addr < MMAP_BOTTOM)
			addr = MMAP_TOP;
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_mmapfixed()
 *	stress mmap at fixed hinted addresses
 */
static int stress_mmapfixed(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mmapfixed_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_mmapfixed_info = {
	.stressor = stress_mmapfixed,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
