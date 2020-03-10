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
	{ NULL,	"mlock N",	"start N workers exercising mlock/munlock" },
	{ NULL,	"mlock-ops N",	"stop after N mlock bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(_POSIX_MEMLOCK_RANGE) && defined(HAVE_MLOCK)

#define MLOCK_MAX	(256*1024)

#if defined(HAVE_MLOCK2)

#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif

/*
 *  do_mlock()
 *	if mlock2 is available, randomly exercise this
 *	or mlock.  If not available, just fallback to
 *	mlock.  Also, pick random mlock2 flags
 */
static int do_mlock(const void *addr, size_t len)
{
	static bool use_mlock2 = true;

	if (use_mlock2) {
		uint32_t rnd = stress_mwc32() >> 5;
		/* Randomly use mlock2 or mlock */
		if (rnd & 1) {
			const int flags = (rnd & 2) ?
				0 : MLOCK_ONFAULT;
			int ret;

			ret = shim_mlock2(addr, len, flags);
			if (!ret)
				return 0;
			if (errno != ENOSYS)
				return ret;

			/* mlock2 not supported... */
			use_mlock2 = false;
		}
	}

	/* Just do mlock */
	return shim_mlock((const void *)addr, len);
}
#else
static inline int do_mlock(const void *addr, size_t len)
{
	return shim_mlock((const void *)addr, len);
}
#endif

static int stress_mlock_child(const stress_args_t *args, void *context)
{
	size_t i, n;
	uint8_t **mappings;
	const size_t page_size = args->page_size;
	size_t max = sysconf(_SC_MAPPED_FILES);
	max = max > MLOCK_MAX ? MLOCK_MAX : max;

	(void)context;

	if ((mappings = calloc(max, sizeof(*mappings))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	do {
		for (n = 0; keep_stressing_flag() && (n < max); n++) {
			int ret;
			if (!keep_stressing())
				break;

			mappings[n] = (uint8_t *)mmap(NULL, page_size * 3,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if (mappings[n] == MAP_FAILED)
				break;
			/*
			 *  Attempt a bogos mlock, ignore failure
			 */
			(void)do_mlock((void *)(mappings[n] + page_size), 0);

			/*
			 *  Attempt a correct mlock
			 */
			ret = do_mlock((void *)(mappings[n] + page_size), page_size);
			if (ret < 0) {
				if (errno == EAGAIN)
					continue;
				if (errno == ENOMEM)
					break;
				pr_fail_err("mlock");
				break;
			} else {
				/*
				 * Mappings are always page aligned so
				 * we can use the bottom bit to
				 * indicate if the page has been
				 * mlocked or not
				 */
				mappings[n] = (uint8_t *)
					((ptrdiff_t)mappings[n] | 1);
				inc_counter(args);
			}
		}

		for (i = 0; i < n;  i++) {
			ptrdiff_t addr = (ptrdiff_t)mappings[i];
			ptrdiff_t mlocked = addr & 1;

			addr ^= mlocked;
			if (mlocked)
				(void)shim_munlock((void *)((uint8_t *)addr + page_size), page_size);
			/*
			 *  Attempt a bogos munlock, ignore failure
			 */
			(void)shim_munlock((void *)((uint8_t *)addr + page_size), 0);
			(void)munmap((void *)addr, page_size * 3);
		}
#if defined(HAVE_MLOCKALL)
#if defined(MCL_CURRENT)
		(void)shim_mlockall(MCL_CURRENT);
#endif
#if defined(MCL_FUTURE)
		(void)shim_mlockall(MCL_FUTURE);
#endif
#if defined(MCL_ONFAULT)
		(void)shim_mlockall(MCL_ONFAULT);
#endif
#endif
		for (n = 0; keep_stressing_flag() && (n < max); n++) {
			if (!keep_stressing())
				break;

			mappings[n] = (uint8_t *)mmap(NULL, page_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if (mappings[n] == MAP_FAILED)
				break;
		}
#if defined(HAVE_MLOCKALL)
		(void)shim_munlockall();
#endif
		for (i = 0; i < n;  i++)
			(void)munmap((void *)mappings[i], page_size);
	} while (keep_stressing());

	free(mappings);

	return EXIT_SUCCESS;
}

/*
 *  stress_mlock()
 *	stress mlock with pages being locked/unlocked
 */
static int stress_mlock(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mlock_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_mlock_info = {
	.stressor = stress_mlock,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_mlock_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#endif
