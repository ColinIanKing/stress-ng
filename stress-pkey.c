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
#include "core-mmap.h"

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
static int stress_pkey(stress_args_t *args)
{
	uint8_t *pages;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	const size_t pages_size = page_size * PAGES_TO_EXERCISE;

	pages = stress_mmap_populate(NULL, pages_size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu pages%s, errno=%d (%s), "
			"skipping stressor\n",
			args->name, pages_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(pages, pages_size, "pkey-pages");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int pkey, ret;
		const size_t page_num = (size_t)stress_mwc8modn(PAGES_TO_EXERCISE);
		const size_t page_offset = page_num * args->page_size;
		uint8_t *page = pages + page_offset;

		/* Exercise invalid pkey flags */
		pkey = shim_pkey_alloc(~0U, 0);
		if (UNLIKELY(pkey >= 0))
			(void)shim_pkey_free(pkey);

		/* Exercise invalid pkey access_rights */
		pkey = shim_pkey_alloc(0, (unsigned int)~0);
		if (UNLIKELY(pkey >= 0))
			(void)shim_pkey_free(pkey);

		/* Exercise invalid pkey free */
		VOID_RET(int, shim_pkey_free(-1));
		VOID_RET(int, shim_pkey_free(INT_MAX));

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
		if (UNLIKELY(ret < 0)) {
			if (errno == ENOSYS) {
				if (stress_instance_zero(args)) {
					pr_inf_skip("%s: pkey system calls not implemented, skipping\n",
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
#if defined(PROT_GROWSDOWN) &&	\
    defined(PROT_GROWSUP)
		(void)shim_pkey_mprotect(page, page_size,
			PROT_READ | PROT_WRITE | PROT_GROWSDOWN | PROT_GROWSUP, pkey);
#endif
		/* Exercise invalid start address, EINVAL */
		(void)shim_pkey_mprotect(page + 7, page_size, PROT_READ, pkey);

		/* Exercise page wrap around, ENOMEM */
		(void)shim_pkey_mprotect((void *)(~(uintptr_t)0 & ~((uintptr_t)page_size -1)),
			 page_size << 1, PROT_READ, pkey);

		/* Exercise zero size, should be OK */
		(void)shim_pkey_mprotect(page, 0, PROT_READ, pkey);

		if (LIKELY(pkey >= 0)) {
			int rights;

			rights = shim_pkey_get(pkey);
			if (rights > -1)
				(void)shim_pkey_set(pkey, (unsigned int)rights);
			(void)shim_pkey_free(pkey);
		}
		/*
		 * Perform an invalid pkey free to exercise the
		 * kernel a bit, will return -EINVAL, we ignore
		 * failures for now.
		 */
		(void)shim_pkey_free(-1);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)pages, pages_size);
	return rc;
}

const stressor_info_t stress_pkey_info = {
	.stressor = stress_pkey,
	.classifier = CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_pkey_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without pkey_mprotect() system call"
};
#endif
