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
	{ "B N","bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,	"bigheap-ops N",	"stop after N bogo bigheap operations" },
	{ NULL,	"bigheap-growth N",	"grow heap by N bytes per iteration" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_bigheap_growth()
 *  	Set bigheap growth from given opt arg string
 */
static int stress_set_bigheap_growth(const char *opt)
{
	uint64_t bigheap_growth;

	bigheap_growth = stress_get_uint64_byte(opt);
	stress_check_range_bytes("bigheap-growth", bigheap_growth,
		MIN_BIGHEAP_GROWTH, MAX_BIGHEAP_GROWTH);
	return stress_set_setting("bigheap-growth", TYPE_ID_UINT64, &bigheap_growth);
}

static int stress_bigheap_child(const stress_args_t *args, void *context)
{
	uint64_t bigheap_growth = DEFAULT_BIGHEAP_GROWTH;
	void *ptr = NULL, *last_ptr = NULL;
	const size_t page_size = args->page_size;
	const size_t stride = page_size;
	size_t size = 0;
	uint8_t *last_ptr_end = NULL;

	(void)context;

	if (!stress_get_setting("bigheap-growth", &bigheap_growth)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bigheap_growth = MAX_BIGHEAP_GROWTH;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bigheap_growth = MIN_BIGHEAP_GROWTH;
	}
	if (bigheap_growth < page_size)
		bigheap_growth = page_size;

	/* Round growth size to nearest page size */
	bigheap_growth &= ~(page_size - 1);

	do {
		void *old_ptr = ptr;
		size += (size_t)bigheap_growth;

		/*
		 * With many instances running it is wise to
		 * double check before the next realloc as
		 * sometimes process start up is delayed for
		 * some time and we should bail out before
		 * exerting any more memory pressure
		 */
		if (!keep_stressing())
			goto abort;

		ptr = realloc(old_ptr, size);
		if (ptr == NULL) {
			pr_dbg("%s: out of memory at %" PRIu64
				" MB (instance %d)\n",
				args->name, (uint64_t)(4096ULL * size) >> 20,
				args->instance);
			free(old_ptr);
			size = 0;
		} else {
			size_t i, n;
			uint8_t *u8ptr, *tmp;

			if (last_ptr == ptr) {
				tmp = u8ptr = last_ptr_end;
				n = (size_t)bigheap_growth;
			} else {
				tmp = u8ptr = ptr;
				n = size;
			}
			if (!keep_stressing())
				goto abort;

			if (page_size > 0) {
				size_t sz = page_size - 1;
				uintptr_t pg_ptr = ((uintptr_t)ptr + sz) & ~sz;
				size_t len = size - (pg_ptr - (uintptr_t)ptr);
				(void)stress_mincore_touch_pages_interruptible((void *)pg_ptr, len);
			}

			for (i = 0; i < n; i+= stride, u8ptr += stride) {
				if (!keep_stressing())
					goto abort;
				*u8ptr = (uint8_t)i;
			}

			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (i = 0; i < n; i+= stride, tmp += stride) {
					if (!keep_stressing())
						goto abort;
					if (*tmp != (uint8_t)i)
						pr_fail("%s: byte at location %p was 0x%" PRIx8
							" instead of 0x%" PRIx8 "\n",
							args->name, (void *)u8ptr, *tmp, (uint8_t)i);
				}
			}
			last_ptr = ptr;
			last_ptr_end = u8ptr;
		}
		inc_counter(args);
	} while (keep_stressing());
abort:
	free(ptr);

	return EXIT_SUCCESS;
}

/*
 *  stress_bigheap()
 *	stress heap allocation
 */
static int stress_bigheap(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_bigheap_child, STRESS_OOMABLE_NORMAL);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_bigheap_growth,	stress_set_bigheap_growth },
	{ 0,			NULL },
};

stressor_info_t stress_bigheap_info = {
	.stressor = stress_bigheap,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
