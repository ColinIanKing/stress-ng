/*
 * Copyright (C) 2025      Colin Ian King.
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

#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-out-of-memory.h"

#define MMAPCOW_FREE	(0x0001)

static const stress_help_t help[] = {
	{ NULL,	"mmapcow N",      "start N workers stressing copy-on-write and munmaps" },
	{ NULL,	"mmapcow-ops N",  "stop after N mmapcow bogo operations" },
	{ NULL,	"mmapcow-free",	  "use madvise(MADV_FREE) on each page before munmapping" },
	{ NULL,	NULL,             NULL }
};

static void OPTIMIZE3 stress_mmapcow_cow_unmap(
	stress_args_t *args,
	uint8_t *page,
	const size_t page_size,
	const int flags)
{
	uint8_t *ptr, *ptr_end = page + page_size;
	uint64_t val = stress_mwc64() | 0x1248124812481248ULL;	/* random, and never zero */

	(void)flags;

	for (ptr = page; ptr < ptr_end; ptr += 64) {
		*(uint64_t *)ptr = val;
	}
	stress_cpu_data_cache_flush(page, page_size);

#if defined(MADV_FREE)
	if (flags & MMAPCOW_FREE)
		(void)madvise(page, page_size, MADV_FREE);
#endif

	if (UNLIKELY(munmap(page, page_size) < 0)) {
		switch (errno) {
		case ENOMEM:
			break;
		default:
			pr_fail("%s: munmap of page at %p failed, errno=%d (%s)\n",
				args->name, page, errno, strerror(errno));
			break;
		}
	}
	stress_bogo_inc(args);
}

static int stress_mmapcow_child(stress_args_t *args, void *ctxt)
{
	size_t mmap_size, max_mmap_size = 0;
	const size_t page_size = args->page_size;
	const size_t page_size2 = page_size + page_size;
	char tmp[32];
	bool mmapcow_free = false;
	int flags = 0;

	(void)ctxt;

	(void)stress_get_setting("mmapcow-free", &mmapcow_free);
#if defined(MADV_FREE)
	flags |= mmapcow_free ? MMAPCOW_FREE : 0;
#else
	if (args->instance == 0)
		pr_inf("%s: madvise(MADV_FREE) not available, not enabling "
			"option --mmapcow-free\n", args->name);
#endif

	mmap_size = page_size;
	do {
		uint8_t *buf = NULL, *buf_end, *ptr;
		uint8_t rnd;
		size_t stride, n_pages, i, offset;

		n_pages = mmap_size / page_size;

		buf = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (buf == MAP_FAILED) {
			if (mmap_size == page_size) {
				pr_inf("%s: failed to mmap %zd bytes, terminating early\n",
					args->name, mmap_size);
				return EXIT_NO_RESOURCE;
			}
			mmap_size = page_size;
			continue;
		}

		/* Low memory? Start again.. */
		if (stress_low_memory(64 * page_size)) {
			(void)munmap(buf, mmap_size);
			mmap_size = page_size;
			continue;
		}
		stress_set_vma_anon_name(buf, mmap_size, "mmapcow-pages");
		buf_end = buf + mmap_size;

		rnd = stress_mwc8() % 6;

		switch (rnd) {
		case 0:
			/* Forward */
			for (ptr = buf; stress_continue(args) && (ptr < buf_end); ptr += page_size) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			break;
		case 1:
			/* Foward stride even pages then odd pages */
			for (ptr = buf; stress_continue(args) && (ptr < buf_end); ptr += page_size2) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			for (ptr = buf + page_size; stress_continue(args) && (ptr < buf_end); ptr += page_size2) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			break;
		case 2:
			/* Forward prime stride */
			stride = stress_get_prime64(n_pages) * page_size;
			for (i = 0, offset = 0; stress_continue(args) && i < n_pages; i++) {
				stress_mmapcow_cow_unmap(args, buf + offset, page_size, flags);

				offset += stride;
				offset %= mmap_size;
			}
			break;
		case 3:
			/* Reverse */
			for (ptr = buf + mmap_size - page_size; stress_continue(args) && (ptr >= buf); ptr -= page_size) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			break;
		case 4:
			/* Reverse stride even pages then odd pages */
			for (ptr = buf + mmap_size - page_size; stress_continue(args) && (ptr >= buf); ptr -= page_size2) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			for (ptr = buf + mmap_size - page_size2; stress_continue(args) && (ptr >= buf); ptr -= page_size2) {
				stress_mmapcow_cow_unmap(args, ptr, page_size, flags);
			}
			break;
		case 5:
			/* Populate 1 random page, unmap all */
			offset = stress_mwc64modn(n_pages) * page_size;
			(void)shim_memset(buf + offset, 0xff, page_size);
#if defined(MADV_FREE)
			if (flags & MMAPCOW_FREE)
				(void)madvise(buf + offset, page_size, MADV_FREE);
#endif
			(void)munmap(buf, mmap_size);
			stress_bogo_inc(args);
			break;
		default:
			break;
		}
		if (mmap_size > max_mmap_size)
			max_mmap_size = mmap_size;

		mmap_size = mmap_size + mmap_size;

		/* Handle unlikely wrap */
		if (UNLIKELY(mmap_size < page_size))
			mmap_size = page_size;
	} while (stress_continue(args));

	stress_uint64_to_str(tmp, sizeof(tmp), max_mmap_size, 0, true);
	pr_dbg("%s: max mmap size: %zd x %zdK pages (%s)\n", args->name,
		max_mmap_size / page_size, page_size >> 10, tmp);

	return EXIT_SUCCESS;
}

/*
 *  stress_mmapcow()
 *	stress mmap, Copy-on-Write and munmap
 */
static int stress_mmapcow(stress_args_t *args)
{
	int ret;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, NULL, stress_mmapcow_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_mmapcow_free, "mmapcow-free", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

const stressor_info_t stress_mmapcow_info = {
	.stressor = stress_mmapcow,
	.class = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help
};
