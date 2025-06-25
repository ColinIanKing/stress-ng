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

#define MIN_SKIPLIST_SIZE	(1 * KB)
#define MAX_SKIPLIST_SIZE	(4 * MB)
#define DEFAULT_SKIPLIST_SIZE	(1 * KB)

typedef struct skip_node {
	unsigned long int value;
	struct skip_node **skip_nodes;
} skip_node_t;

typedef struct {
	size_t level;
	size_t max_level;
	skip_node_t *head;
} skip_list_t;

static const stress_help_t help[] = {
	{ NULL,	"skiplist N",	  "start N workers that exercise a skiplist search" },
	{ NULL,	"skiplist-ops N", "stop after N skiplist search bogo operations" },
	{ NULL,	"skiplist-size N", "number of 32 bit integers to add to skiplist" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  skip_list_random_level()
 *	generate a quasi-random skip list level
 */
static inline size_t OPTIMIZE3 skip_list_random_level(const size_t max_level)
{
	register size_t level = 1;
	register size_t r = stress_mwc8(); /* 8 bits -> 2^256 is more than enough */

	while ((r & 1) && (level < max_level)) {
		r >>= 1;
		level++;
	}
	return level;
}

/*
 *  skip_node_alloc()
 *	allocate a skip list node
 */
static inline skip_node_t *skip_node_alloc(const size_t levels)
{
	const size_t sz = sizeof(skip_node_t) + ((levels + 1) * sizeof(skip_node_t *));
	skip_node_t *skip_node;

	skip_node = calloc(1, sz);
	if (!skip_node)
		return NULL;
	skip_node->skip_nodes = (skip_node_t **)(skip_node + 1);
	return skip_node;
}

/*
 *  skip_list_init
 *	initialize the skip list, return NULL if failed
 */
static skip_list_t *skip_list_init(skip_list_t *list, const size_t max_level)
{
	register size_t i;
	register skip_node_t *head;

	head = skip_node_alloc(max_level);
	if (UNLIKELY(!head))
		return NULL;
	list->level = 1;
	list->max_level = max_level;
	list->head = head;
	head->value = INT_MAX;

	for (i = 0; i <= max_level; i++)
		list->head->skip_nodes[i] = list->head;

	return list;
}

/*
 *  skip_list_insert()
 *	insert a value into the skiplist
 */
static skip_node_t OPTIMIZE3 *skip_list_insert(skip_list_t *list, const unsigned long int value)
{
	skip_node_t **skip_nodes;
	skip_node_t *skip_node = list->head;
	register size_t i, level;

	skip_nodes = (skip_node_t **)calloc(list->max_level + 1, sizeof(*skip_nodes));
	if (UNLIKELY(!skip_nodes))
		return NULL;

	for (i = list->level; i >= 1; i--) {
		while (skip_node->skip_nodes[i]->value < value)
			skip_node = skip_node->skip_nodes[i];

		skip_nodes[i] = skip_node;
	}
	skip_node = skip_node->skip_nodes[1];

	if (value == skip_node->value) {
		skip_node->value = value;
		free(skip_nodes);
		return skip_node;
	}

	level = skip_list_random_level(list->max_level);
	if (level > list->level) {
		for (i = list->level + 1; i <= level; i++)
			skip_nodes[i] = list->head;

		list->level = level;
	}

	if (UNLIKELY(level < 1)) {
		free(skip_nodes);
		return NULL;
	}
	skip_node = skip_node_alloc(level);
	if (UNLIKELY(!skip_node)) {
		free(skip_nodes);
		return NULL;
	}
	skip_node->value = value;
	for (i = 1; i <= level; i++) {
		skip_node->skip_nodes[i] = skip_nodes[i]->skip_nodes[i];
		skip_nodes[i]->skip_nodes[i] = skip_node;
	}
	free(skip_nodes);
	return skip_node;
}

/*
 *  skip_list_search()
 *	search the skiplist for a specific value
 */
static skip_node_t OPTIMIZE3 *skip_list_search(skip_list_t *list, const unsigned long int value)
{
	skip_node_t *skip_node = list->head;
	register size_t i;

	for (i = list->level; i >= 1; i--) {
		while (skip_node->skip_nodes[i]->value < value)
			skip_node = skip_node->skip_nodes[i];
	}
	return skip_node->skip_nodes[1]->value == value ? skip_node->skip_nodes[1] : NULL;
}

/*
 *  skip_list_ln2()
 *	compute maximum skiplist level
 */
static inline unsigned long int OPTIMIZE3 skip_list_ln2(register unsigned long int n)
{
#if defined(HAVE_BUILTIN_CLZL)
	/* this is fine as long as n > 0 */
	return (sizeof(n) * 8) - __builtin_clzl(n);
#else
	register unsigned long int i = 0;

	while (n) {
		i++;
		n >>= 1;
	}
	return i;
#endif
}

/*
 *  skip_list_free()
 *	free a skip list
 */
static void skip_list_free(skip_list_t *list)
{
	skip_node_t *head = list->head;
	skip_node_t *skip_node = head;

	while (skip_node && (skip_node->skip_nodes[1] != head)) {
		skip_node_t *next = skip_node->skip_nodes[1];

		free(skip_node);
		skip_node = next;
	}
	if (skip_node)
		free(skip_node);
}

/*
 *  stress_skiplist()
 *	stress skiplist
 */
static int OPTIMIZE3 stress_skiplist(stress_args_t *args)
{
	unsigned long int n, i, ln2n;
	uint64_t skiplist_size = DEFAULT_SKIPLIST_SIZE;
	int rc = EXIT_FAILURE;

	if (!stress_get_setting("skiplist-size", &skiplist_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			skiplist_size = MAX_SKIPLIST_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			skiplist_size = MIN_SKIPLIST_SIZE;
	}
	n = (unsigned long int)skiplist_size;
	ln2n = skip_list_ln2(n);

	/*
	 *  This stops static analyzers getting confused for
	 *  sizes where they assume ln2n is 0
	 */
	if (ln2n < 1) {
		pr_fail("%s: unexpected ln base 2 of %lu is less than 1 (should not occur)\n",
			args->name, n);
		rc = EXIT_FAILURE;
		goto finish;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		skip_list_t list;

		if (!skip_list_init(&list, ln2n)) {
			pr_inf("%s: out of memory initializing the skip list%s\n",
				args->name, stress_get_memfree_str());
			return EXIT_NO_RESOURCE;
		}

		for (i = 0; i < n; i++) {
			const unsigned long int v = (i >> 1) ^ i;

			if (UNLIKELY(!skip_list_insert(&list, v))) {
				pr_inf("%s: out of memory initializing the skip list%s\n",
					args->name, stress_get_memfree_str());
				skip_list_free(&list);
				return EXIT_NO_RESOURCE;
			}
		}

		for (i = 0; i < n; i++) {
			const unsigned long int v = (i >> 1) ^ i;

			if (UNLIKELY(!skip_list_search(&list, v))) {
				pr_fail("%s node containing value %lu was not found\n",
					args->name, v);
				rc = EXIT_FAILURE;
				skip_list_free(&list);
				goto finish;
			}
		}
		skip_list_free(&list);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_skiplist_size, "skiplist-size", TYPE_ID_UINT64, MIN_SKIPLIST_SIZE, MAX_SKIPLIST_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_skiplist_info = {
	.stressor = stress_skiplist,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
