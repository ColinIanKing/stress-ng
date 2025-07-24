/*
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-mmap.h"
#include "core-pragma.h"
#include "core-put.h"

#define MIN_RANDLIST_SIZE		(1)
#define MAX_RANDLIST_SIZE		(8192)
#define DEFAULT_RANDLIST_SIZE		(64)

#define MIN_RANDLIST_ITEMS		(1)
#define MAX_RANDLIST_ITEMS		(0xffffffffULL)
#define DEFAULT_RANDLIST_ITEMS		(100000)

#define STRESS_RANDLIST_ALLOC_HEAP	(0)
#define STRESS_RANDLIST_ALLOC_MMAP	(1)

static const stress_help_t help[] = {
	{ NULL,	"randlist N",		"start N workers that exercise random ordered list" },
	{ NULL, "randlist-compact",	"reduce mmap and malloc overheads" },
	{ NULL, "randlist-items N",	"number of items in the random ordered list" },
	{ NULL,	"randlist-ops N",	"stop after N randlist bogo no-op operations" },
	{ NULL, "randlist-size N",	"size of data in each item in the list" },
	{ NULL,	NULL,			NULL }
};

typedef struct stress_randlist_item {
	struct stress_randlist_item *next;
	uint8_t dataval;
	uint8_t alloc_type:1;
	uint8_t data[];
} stress_randlist_item_t;

static void stress_randlist_free_item(stress_randlist_item_t **item, const size_t randlist_size)
{
	if (UNLIKELY(!*item))
		return;

	if ((*item)->alloc_type == STRESS_RANDLIST_ALLOC_HEAP)
		free(*item);
	else if ((*item)->alloc_type == STRESS_RANDLIST_ALLOC_MMAP) {
		const size_t size = sizeof(**item) + randlist_size;

		(void)munmap((void *)*item, size);
	}

	*item = NULL;
}

static void stress_randlist_free_ptrs(
	stress_randlist_item_t *compact_ptr,
	stress_randlist_item_t *ptrs[],
	const size_t n,
	const size_t randlist_size)
{
	if (compact_ptr) {
		free(compact_ptr);
	} else {
		size_t i;

		for (i = 0; i < n; i++) {
			stress_randlist_free_item(&ptrs[i], randlist_size);
		}
	}
	free(ptrs);
}

static void stress_randlist_enomem(stress_args_t *args)
{
	pr_inf_skip("%s: cannot allocate the list, skipping stressor\n",
		args->name);
}

static inline uint8_t OPTIMIZE3 stress_randlist_bad_data(
	const stress_randlist_item_t *ptr,
	const size_t randlist_size)
{
	register const uint8_t *data = ptr->data;
	register const uint8_t *end = data + randlist_size;
	register const uint8_t dataval = ptr->dataval;

PRAGMA_UNROLL_N(8)
	while (data < end) {
		if (UNLIKELY(*(data++) != dataval))
			return true;
	}
	return false;
}

static inline void OPTIMIZE3 stress_randlist_exercise(
	stress_args_t *args,
	stress_randlist_item_t *head,
	const size_t randlist_size,
	const bool verify,
	int *rc)
{
	register stress_randlist_item_t *ptr;
	uint8_t dataval = stress_mwc8();

	for (ptr = head; ptr; ptr = ptr->next) {
		shim_builtin_prefetch(ptr->next);
		ptr->dataval = dataval;
		(void)shim_memset(ptr->data, dataval, randlist_size);
		dataval++;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}

	for (ptr = head; ptr; ptr = ptr->next) {
		shim_builtin_prefetch(ptr->next);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(verify && stress_randlist_bad_data(ptr, randlist_size))) {
			pr_fail("%s: data check failure in list object at 0x%p\n", args->name, ptr);
			*rc = EXIT_FAILURE;
		}
	}
}

/*
 *  stress_randlist()
 *	stress a list containing random values
 */
static int stress_randlist(stress_args_t *args)
{
	register size_t i;
	stress_randlist_item_t **ptrs;
	stress_randlist_item_t *ptr, *head, *next;
	stress_randlist_item_t *compact_ptr = NULL;
	bool do_mmap = false;
	bool randlist_compact = false;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	size_t randlist_items = DEFAULT_RANDLIST_ITEMS;
	size_t randlist_size = DEFAULT_RANDLIST_SIZE;
	size_t heap_allocs = 0;
	size_t mmap_allocs = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("randlist-compact", &randlist_compact);
	if (!stress_get_setting("randlist-items", &randlist_items)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			randlist_items = MAX_RANDLIST_ITEMS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			randlist_items = MIN_RANDLIST_ITEMS;
	}
	if (!stress_get_setting("randlist-size", &randlist_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			randlist_size = MAX_RANDLIST_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			randlist_size = MIN_RANDLIST_SIZE;
	}

	if (randlist_size >= args->page_size)
		do_mmap = true;

	ptrs = (stress_randlist_item_t **)calloc(randlist_items, sizeof(stress_randlist_item_t *));
	if (!ptrs) {
		pr_inf_skip("%s: cannot allocate %zu temporary pointers%s, skipping stressor\n",
			args->name, randlist_items, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	if (randlist_compact) {
		const size_t size = sizeof(*ptr) + randlist_size;

		compact_ptr = (stress_randlist_item_t *)calloc(randlist_items, size);
		if (!compact_ptr) {
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			free(ptrs);
			stress_randlist_enomem(args);
			return EXIT_NO_RESOURCE;
		}

		for (ptr = compact_ptr, i = 0; i < randlist_items; i++) {
			if (UNLIKELY(!stress_continue_flag())) {
				stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
				stress_randlist_free_ptrs(compact_ptr, ptrs, i, randlist_size);
				stress_randlist_enomem(args);
				return EXIT_SUCCESS;
			}
			ptrs[i] = ptr;
			ptr = (stress_randlist_item_t *)((uintptr_t)ptr + size);
		}
		heap_allocs++;
	} else {
		for (i = 0; i < randlist_items; i++) {
			const size_t size = sizeof(*ptr) + randlist_size;

			if (UNLIKELY(!stress_continue_flag())) {
				stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
				stress_randlist_free_ptrs(compact_ptr, ptrs, i, randlist_size);
				return EXIT_SUCCESS;
			}
retry:
			if (do_mmap && (stress_mwc8() < 16)) {
				ptr = (stress_randlist_item_t *)stress_mmap_populate(NULL, size,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (UNLIKELY(ptr == MAP_FAILED)) {
					do_mmap = false;
					goto retry;
				}
				ptr->alloc_type = STRESS_RANDLIST_ALLOC_MMAP;
				mmap_allocs++;
			} else {
				ptr = (stress_randlist_item_t *)calloc(1, size);
				if (UNLIKELY(!ptr)) {
					stress_randlist_free_ptrs(compact_ptr, ptrs, i, randlist_size);
					stress_randlist_enomem(args);
					return EXIT_NO_RESOURCE;
				}
				ptr->alloc_type = STRESS_RANDLIST_ALLOC_HEAP;
				heap_allocs++;
			}
			ptrs[i] = ptr;
		}
	}

	/*
	 *  Shuffle into random item order
	 */
	for (i = 0; i < randlist_items; i++) {
		size_t n = (size_t)stress_mwc32modn(randlist_items);

		ptr = ptrs[i];
		ptrs[i] = ptrs[n];
		ptrs[n] = ptr;

		if (UNLIKELY(!stress_continue_flag())) {
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			stress_randlist_free_ptrs(compact_ptr, ptrs, i, randlist_size);
			return EXIT_SUCCESS;
		}
	}

	/*
	 *  Link all items together based on the random ordering
	 */
	for (i = 0; i < randlist_items; i++) {
		ptr = ptrs[i];
		ptr->next = (i == randlist_items - 1) ? NULL : ptrs[i + 1];
	}

	head = ptrs[0];
	free(ptrs);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_randlist_exercise(args, head, randlist_size, verify, &rc);
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	pr_dbg("%s: heap allocations: %zd, mmap allocations: %zd\n", args->name, heap_allocs, mmap_allocs);

	if (compact_ptr) {
		free(compact_ptr);
	} else {
		for (ptr = head; ptr; ) {
			next = ptr->next;
			stress_randlist_free_item(&ptr, randlist_size);
			ptr = next;
		}
	}

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_randlist_compact,	"randlist-compact", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_randlist_items,	"randlist-items",   TYPE_ID_SIZE_T, MIN_RANDLIST_ITEMS, MAX_RANDLIST_ITEMS, NULL },
	{ OPT_randlist_size,	"randlist-size",    TYPE_ID_SIZE_T, MIN_RANDLIST_SIZE, MAX_RANDLIST_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_randlist_info = {
	.stressor = stress_randlist,
	.classifier = CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
