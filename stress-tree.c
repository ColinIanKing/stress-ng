/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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

struct tree_node;

typedef void (*stress_tree_func)(const args_t *args,
				 const size_t n,
				 struct tree_node *data);

typedef struct {
        const char              *name;  /* human readable form of stressor */
        const stress_tree_func   func;	/* the tree method function */
} stress_tree_method_info_t;

static const stress_tree_method_info_t tree_methods[];

#if defined(HAVE_LIB_BSD) && !defined(__APPLE__)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

struct tree_node {
	union {
		RB_ENTRY(tree_node) rb_entry;
		SPLAY_ENTRY(tree_node) splay_entry;
	};
	uint64_t value;
};

#endif

/*
 *  stress_set_tree_size()
 *	set tree size
 */
void stress_set_tree_size(const void *opt)
{
	uint64_t tree_size;

	tree_size = get_uint64(opt);
	check_range("tree-size", tree_size,
		MIN_TREE_SIZE, MAX_TREE_SIZE);
	set_setting("tree-size", TYPE_ID_UINT64, &tree_size);
}

#if defined(HAVE_LIB_BSD) && !defined(__APPLE__)

/*
 *  stress_tree_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED stress_tree_handler(int dummy)
{
	(void)dummy;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

static int tree_node_cmp_fwd(struct tree_node *n1, struct tree_node *n2)
{
	return n1->value < n2->value;
}

static int tree_node_cmp_rev(struct tree_node *n1, struct tree_node *n2)
{
	return n1->value > n2->value;
}


RB_HEAD(rb_tree, tree_node);
RB_PROTOTYPE(rb_tree, tree_node, rb_entry, tree_node_cmp_fwd);
RB_GENERATE(rb_tree, tree_node, rb_entry, tree_node_cmp_fwd);

SPLAY_HEAD(splay_tree, tree_node);
SPLAY_PROTOTYPE(splay_tree, tree_node, splay_entry, tree_node_cmp_rev);
SPLAY_GENERATE(splay_tree, tree_node, splay_entry, tree_node_cmp_rev);

static void stress_tree_rb(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	struct rb_tree head = RB_INITIALIZER(&head);
	size_t i;
	register struct tree_node *node, *next;
	
	(void)args;
	
	for (node = data, i = 0; i < n; i++, node++) {
		RB_INSERT(rb_tree, &head, node);
	}
	for (node = data, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = RB_FIND(rb_tree, &head, node);
		if (!find)
			pr_err("%s: rb tree node #%zd node found\n",
				args->name, i);
	}
	for (node = RB_MIN(rb_tree, &head); node; node = next) {
		next = RB_NEXT(rb_tree, &head, node);
		RB_REMOVE(rb_tree, &head, node);
	}
}

static void stress_tree_splay(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	struct splay_tree head = SPLAY_INITIALIZER(&head);
	size_t i;
	register struct tree_node *node, *next;
	
	(void)args;
	
	for (node = data, i = 0; i < n; i++, node++) {
		SPLAY_INSERT(splay_tree, &head, node);
	}
	for (node = data, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = SPLAY_FIND(splay_tree, &head, node);
		if (!find)
			pr_err("%s: splay tree node #%zd node found\n",
				args->name, i);
	}
	for (node = SPLAY_MIN(splay_tree, &head); node; node = next) {
		next = SPLAY_NEXT(splay_tree, &head, node);
		SPLAY_REMOVE(splay_tree, &head, node);
	}
}

static void stress_tree_all(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	stress_tree_rb(args, n, data);
	stress_tree_splay(args, n, data);
}
#endif

/*
 * Table of tree stress methods
 */
static const stress_tree_method_info_t tree_methods[] = {
#if defined(HAVE_LIB_BSD) && !defined(__APPLE__)
	{ "all",	stress_tree_all },
	{ "rb",		stress_tree_rb },
	{ "splay",	stress_tree_splay },
#endif
	{ NULL,		NULL },
};

/*
 *  stress_set_tree_method()
 *	set the default funccal stress method
 */
int stress_set_tree_method(const char *name)
{
	stress_tree_method_info_t const *info;

	for (info = tree_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("tree-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "tree-method must be one of:");
	for (info = tree_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

#if defined(HAVE_LIB_BSD) && !defined(__APPLE__)
/*
 *  stress_tree()
 *	stress tree
 */
int stress_tree(const args_t *args)
{
	uint64_t tree_size = DEFAULT_TREE_SIZE;
	struct tree_node *data, *node;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	if (!get_setting("tree-size", &tree_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			tree_size = MAX_TREE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			tree_size = MIN_TREE_SIZE;
	}
	n = (size_t)tree_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_tree_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	for (node = data, i = 0; i < n; i++, node++)
		node->value = mwc64();

	do {
		uint64_t rnd;

		stress_tree_rb(args, n, data);
		stress_tree_splay(args, n, data);

		rnd = mwc64();
		for (node = data, i = 0; i < n; i++, node++)
			node->value ^= rnd;

		inc_counter(args);
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	free(data);

	return EXIT_SUCCESS;
}
#else
int stress_tree(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
