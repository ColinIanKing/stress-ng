/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define PAGES_TO_EXERCISE	(8)

static const stress_help_t help[] = {
	{ NULL,	"pkey N",	"start N workers exercising pkey_mprotect" },
	{ NULL,	"pkey-ops N",	"stop after N bogo pkey_mprotect bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_PKEY_MPROTECT)

/*
 *  stress_pkey()
 *	stress pkeys
 */
static int stress_pkey(const stress_args_t *args)
{
	uint8_t *pages;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	const size_t pages_size = page_size * PAGES_TO_EXERCISE;

	pages = mmap(NULL, pages_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED) {
		pr_inf("%s: cannot allocate a page, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int pkey, ret;
		const int page_num = (stress_mwc8() % PAGES_TO_EXERCISE);
		const size_t page_offset = page_num * args->page_size;
		uint8_t *page = pages + page_offset;

		/* Exercise invalid pkey flags */
		pkey = shim_pkey_alloc(~0, 0);
		if (pkey >= 0)
			(void)shim_pkey_free(pkey);

#if defined(PKEY_DISABLE_WRITE)
		/* Use PKEY_DISABLE_WRITE if it's defined */
		pkey = shim_pkey_alloc(0, PKEY_DISABLE_WRITE);
		if (pkey < 0)
			pkey = shim_pkey_alloc(0, 0);
#else
		/* Try 0 flags instead */
		pkey = shim_pkey_alloc(0, 0);
#endif
		if (pkey < 0) {
			/*
			 *  Can't allocate, perhaps we don't have any, or
			 *  the system does not provide support, or the syscall
			 *  was not available. All is not lost, as we can
			 *  perform pkey_mprotect on a -ve pkey, this should
			 *  fall back and perform the standard mprotect call.
			 */
			pkey = -1;
		}

		ret = shim_pkey_mprotect(page, page_size, PROT_NONE, pkey);
		if (ret < 0) {
			if (errno == ENOSYS) {
				if (args->instance == 0) {
					pr_inf("%s: pkey system calls not implemented, skipping\n",
						args->name);
				}
				rc = EXIT_NOT_IMPLEMENTED;
				break;
			}
		}
		(void)shim_pkey_mprotect(page, page_size, PROT_READ, pkey);
		(void)shim_pkey_mprotect(page, page_size, PROT_WRITE, pkey);
		(void)shim_pkey_mprotect(page, page_size, PROT_READ | PROT_WRITE, pkey);

		(void)shim_pkey_mprotect(page, page_size, PROT_EXEC, pkey);
		(void)shim_pkey_mprotect(page, page_size, PROT_READ | PROT_EXEC, pkey);
		(void)shim_pkey_mprotect(page, page_size, PROT_WRITE | PROT_EXEC, pkey);
		(void)shim_pkey_mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC, pkey);

		/* Exercise invalid mprotect flags */
#if defined(PROT_GROWSDOWN) && defined(PROT_GROWSUP)
		(void)shim_pkey_mprotect(page, page_size,
			PROT_READ | PROT_WRITE | PROT_GROWSDOWN | PROT_GROWSUP, pkey);
#endif
		/* Exercise invalid start address, EINVAL */
		(void)shim_pkey_mprotect(page + 7, page_size, PROT_READ, pkey);

		/* Exercise page wrap around, ENOMEM */
		(void)shim_pkey_mprotect((void *)(~0 & ~(page_size -1)),
			 page_size << 1, PROT_READ, pkey);

		/* Exercise zero size, should be OK */
		(void)shim_pkey_mprotect(page, 0, PROT_READ, pkey);

		if (pkey >= 0) {
			int rights;

			rights = shim_pkey_get(pkey);
			if (rights > -1)
				(void)shim_pkey_set(pkey, rights);
			(void)shim_pkey_free(pkey);
		}
		/*
		 * Perform an invalid pkey free to exercise the
		 * kernel a bit, will return -EINVAL, we ignore
		 * failures for now.
		 */
		(void)shim_pkey_free(-1);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(pages, pages_size);
	return rc;
}

stressor_info_t stress_pkey_info = {
	.stressor = stress_pkey,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_pkey_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
