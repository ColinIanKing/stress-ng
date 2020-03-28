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

typedef struct skip_node {
	int value;
	struct skip_node *skip_nodes[1];
} skip_node_t;

typedef struct {
	size_t level;
	size_t max_level;
	skip_node_t *head;
} skip_list_t;

static const stress_help_t help[] =  {
	{ NULL,	"skiplist N",	  "start N workers that exercise a skiplist search" },
	{ NULL,	"skiplist-ops N", "stop after N skiplist search bogo operations" },
	{ NULL,	"skiplist-size N", "number of 32 bit integers to add to skiplist" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  stress_set_skiplist_size()
 *	set skiplist size from given option string
 */
static int stress_set_skiplist_size(const char *opt)
{
	uint64_t skiplist_size;

	skiplist_size = stress_get_uint64(opt);
	stress_check_range("skiplist-size", skiplist_size,
		MIN_SKIPLIST_SIZE, MAX_SKIPLIST_SIZE);
	return stress_set_setting("skiplist-size", TYPE_ID_UINT64, &skiplist_size);
}

/*
 *  skip_list_random_level()
 *	generate a quasi-random skip list level
 */
static inline size_t skip_list_random_level(const size_t max_level)
{
	register size_t level = 1;
	register size_t r = stress_mwc32();

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
static skip_node_t *skip_node_alloc(const size_t levels)
{
	const size_t sz = sizeof(skip_node_t) + (levels * sizeof(skip_node_t *));

	return (skip_node_t *)malloc(sz);
}

/*
 *  skip_list_init
 *	initialize the skip list, return NULL if failed
 */
static skip_list_t *skip_list_init(skip_list_t *list, const size_t max_level)
{
	register size_t i;
	skip_node_t *head;

	head = skip_node_alloc(max_level);
	if (!head)
		return NULL;
	list->level = 1;
	list->max_level = max_level;
	list->head = head;
	head->value = INT_MAX;

	for (i = 0; i <= max_level; i++)
		head->skip_nodes[i] = list->head;

	return list;
}

/*
 *  skip_list_insert()
 *	insert a value into the skiplist
 */
static skip_node_t *skip_list_insert(skip_list_t *list, const int value)
{
	skip_node_t *skip_nodes[list->max_level + 1];
	skip_node_t *skip_node = list->head;
	register size_t i, level;

	for (i = list->level; i >= 1; i--) {
		while (skip_node->skip_nodes[i]->value < value)
			skip_node = skip_node->skip_nodes[i];

		skip_nodes[i] = skip_node;
	}
	skip_node = skip_node->skip_nodes[1];

	if (value == skip_node->value) {
		skip_node->value = value;
		return skip_node;
	}

	level = skip_list_random_level(list->max_level);
	if (level > list->level) {
		for (i = list->level + 1; i <= level; i++)
			skip_nodes[i] = list->head;

		list->level = level;
        }

	skip_node = skip_node_alloc(level);
	if (!skip_node)
		return NULL;
	skip_node->value = value;
	for (i = 1; i <= level; i++) {
		skip_node->skip_nodes[i] = skip_nodes[i]->skip_nodes[i];
		skip_nodes[i]->skip_nodes[i] = skip_node;
	}
	return skip_node;
}

/*
 *  skip_list_search()
 *	search the skiplist for a specific value
 */
static skip_node_t *skip_list_search(skip_list_t *list, const int value)
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
static size_t skip_list_ln2(size_t n)
{
	size_t i = 0;

	while (n) {
		i++;
		n >>= 1;
	}
	return i;
}

#if 0
static void skip_list_dump(skip_list_t *list)
{
	skip_node_t *head = list->head;
	skip_node_t *skip_node = head;

	while (skip_node && skip_node->skip_nodes[1] != head) {
		printf("DUMP: %d\n", skip_node->skip_nodes[1]->value);
		skip_node = skip_node->skip_nodes[1];
	}
}
#endif

/*
 *  skip_list_free()
 *	free a skip list
 */
static void skip_list_free(skip_list_t *list)
{
	skip_node_t *head = list->head;
	skip_node_t *skip_node = head;

	while (skip_node && skip_node->skip_nodes[1] != head) {
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
static int stress_skiplist(const stress_args_t *args)
{
	size_t n, i, ln2n;
	uint64_t skiplist_size = 1024;

	if (!stress_get_setting("skiplist-size", &skiplist_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			skiplist_size = MAX_SKIPLIST_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			skiplist_size = MIN_SKIPLIST_SIZE;
	}
	n = (size_t)skiplist_size;
	ln2n = skip_list_ln2(n);

	do {
		skip_list_t list;

		if (!skip_list_init(&list, ln2n)) {
			pr_inf("%s: out of memory initializing the skip list\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}

		for (i = 0; i < n; i++) {
			int v = (i >> 1) ^ i;

			if (!skip_list_insert(&list, v)) {
				pr_inf("%s: out of memory initializing the skip list\n",
					args->name);
				return EXIT_NO_RESOURCE;
			}
		}

		for (i = 0; i < n; i++) {
			int v = (i >> 1) ^ i;

			if (!skip_list_search(&list, v))
				pr_fail("%s node containing value %d was not found\n",
					args->name, v);
		}
		skip_list_free(&list);

		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_skiplist_size,	stress_set_skiplist_size },
	{ 0,			NULL },
};

stressor_info_t stress_skiplist_info = {
	.stressor = stress_skiplist,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
