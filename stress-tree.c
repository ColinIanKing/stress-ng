/*
 * Copyright (C) 2016-2018 Canonical, Ltd.
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

struct binary_node {
	struct tree_node *left;
	struct tree_node *right;
};

#define LH	0
#define EH	1
#define RH	2

struct avl_node {
	struct tree_node *left;
	struct tree_node *right;
	uint8_t	bf;
};

struct tree_node {
	union {
		RB_ENTRY(tree_node)	rb;
		SPLAY_ENTRY(tree_node)	splay;
		struct binary_node	binary;
		struct avl_node		avl;
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
	if (n1->value == n2->value)
		return 0;
	if (n1->value > n2->value)
		return 1;
	else
		return -1;
}

static RB_HEAD(rb_tree, tree_node) rb_root;
RB_PROTOTYPE(rb_tree, tree_node, rb, tree_node_cmp_fwd);
RB_GENERATE(rb_tree, tree_node, rb, tree_node_cmp_fwd);

static SPLAY_HEAD(splay_tree, tree_node) splay_root;
SPLAY_PROTOTYPE(splay_tree, tree_node, splay, tree_node_cmp_fwd);
SPLAY_GENERATE(splay_tree, tree_node, splay, tree_node_cmp_fwd);

static void stress_tree_rb(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	size_t i;
	register struct tree_node *node, *next;

	RB_INIT(&rb_root);

	for (node = data, i = 0; i < n; i++, node++) {
		register struct tree_node *res;

		res = RB_FIND(rb_tree, &rb_root, node);
		if (!res)
			RB_INSERT(rb_tree, &rb_root, node);
	}
	for (node = data, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = RB_FIND(rb_tree, &rb_root, node);
		if (!find)
			pr_err("%s: rb tree node #%zd node found\n",
				args->name, i);
	}
	for (node = RB_MIN(rb_tree, &rb_root); node; node = next) {
		next = RB_NEXT(rb_tree, &rb_root, node);
		RB_REMOVE(rb_tree, &rb_root, node);
	}
}

static void stress_tree_splay(
	const args_t *args,
	const size_t n,
	struct tree_node *nodes)
{
	size_t i;
	register struct tree_node *node, *next;

	SPLAY_INIT(&splay_root);

	for (node = nodes, i = 0; i < n; i++, node++) {
		register struct tree_node *res;

		res = SPLAY_FIND(splay_tree, &splay_root, node);
		if (!res)
			SPLAY_INSERT(splay_tree, &splay_root, node);
	}
	for (node = nodes, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = SPLAY_FIND(splay_tree, &splay_root, node);
		if (!find)
			pr_err("%s: splay tree node #%zd node found\n",
				args->name, i);
	}
	for (node = SPLAY_MIN(splay_tree, &splay_root); node; node = next) {
		next = SPLAY_NEXT(splay_tree, &splay_root, node);
		SPLAY_REMOVE(splay_tree, &splay_root, node);
	}
}

static void binary_insert(
	struct tree_node **head,
	struct tree_node *node)
{
	while (*head) {
		head = (node->value <= (*head)->value) ?
			&(*head)->binary.left :
			&(*head)->binary.right;
	}
	*head = node;
}

static struct tree_node *binary_find(
	struct tree_node *head,
	struct tree_node *node)
{
	while (head) {
		if (node->value == head->value)
			return head;
		head = (node->value <= head->value) ?
				head->binary.left :
				head->binary.right;
	}
	return NULL;
}

static void binary_remove_tree(struct tree_node *node)
{
	if (node) {
		binary_remove_tree(node->binary.left);
		binary_remove_tree(node->binary.right);
		node->binary.left = NULL;
		node->binary.right = NULL;
	}
}


static void stress_tree_binary(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	size_t i;
	struct tree_node *node, *head = NULL;

	for (node = data, i = 0; i < n; i++, node++) {
		binary_insert(&head, node);
	}

	for (node = data, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = binary_find(head, node);
		if (!find)
			pr_err("%s: binary tree node #%zd node found\n",
				args->name, i);
	}
	binary_remove_tree(head);
}

static void avl_insert(
	struct tree_node **root,
	struct tree_node *node,
	bool *taller)
{
	bool sub_taller = false;
	register struct tree_node *p, *q;

	if (!*root) {
		*root = node;
		(*root)->avl.left = NULL;
		(*root)->avl.right = NULL;
		(*root)->avl.bf = EH;
		*taller = true;
	} else {
		if (node->value < (*root)->value) {
			avl_insert(&(*root)->avl.left, node, &sub_taller);
			if (sub_taller) {
				switch ((*root)->avl.bf) {
				case EH:
					(*root)->avl.bf = LH;
					*taller = true;
					break;
				case RH:
					(*root)->avl.bf = EH;
					*taller = false;
					break;
				case LH:
					/* Rebalance required */
					p = (*root)->avl.left;
					if (p->avl.bf == LH) {
						/* Single rotation */
						(*root)->avl.left = p->avl.right;
						p->avl.right = *root;
						p->avl.bf = EH;
						(*root)->avl.bf = EH;
						*root = p;
					} else {
						/* Double rotation */
						q = p->avl.right;
						(*root)->avl.left = q->avl.right;
						q->avl.right = *root;
						p->avl.right = q->avl.left;
						q->avl.left = p;

						/* Update balance factors */
						switch (q->avl.bf) {
						case RH:
							(*root)->avl.bf = EH;
							p->avl.bf = LH;
							break;
						case LH:
							(*root)->avl.bf = RH;
							p->avl.bf = EH;
							break;
						case EH:
							(*root)->avl.bf = EH;
							p->avl.bf = EH;
							break;
						}
						q->avl.bf = EH;
						*root = q;
					}
					*taller = false;
					break;
				}
			}
		} else if (node->value > (*root)->value) {
			avl_insert(&(*root)->avl.right, node, &sub_taller);
			if (sub_taller) {
				switch ((*root)->avl.bf) {
				case LH:
					(*root)->avl.bf = EH;
					*taller = false;
					break;
				case EH:
					(*root)->avl.bf = RH;
					*taller = true;
					break;
				case RH:
					/* Rebalance required */
					p = (*root)->avl.right;
					if (p->avl.bf == RH) {
						/* Single rotation */
						(*root)->avl.right = p->avl.left;
						p->avl.left = *root;
						p->avl.bf = EH;
						(*root)->avl.bf = EH;
						*root = p;
					} else {
						/* Double rotation */
						q = p->avl.left;
						(*root)->avl.right = q->avl.left;
						q->avl.left = *root;
						p->avl.left = q->avl.right;
						q->avl.right = p;

						/* Update balance factors */
						switch (q->avl.bf) {
						case LH:
							(*root)->avl.bf = EH;
							p->avl.bf = RH;
							break;
						case RH:
							(*root)->avl.bf = LH;
							p->avl.bf = EH;
							break;
						case EH:
							(*root)->avl.bf = EH;
							p->avl.bf = EH;
							break;
						}
						q->avl.bf = EH;
						*root = q;
					}
					*taller = false;
					break;
				}
			} else {
				/* tree not rebalanced.. */
				*taller = false;
			}
		} else {
			*taller = false;
		}
	}
}

static struct tree_node *avl_find(
	struct tree_node *head,
	struct tree_node *node)
{
	while (head) {
		if (node->value == head->value)
			return head;
		head = (node->value <= head->value) ?
				head->avl.left :
				head->avl.right;
	}
	return NULL;
}

static void avl_remove_tree(struct tree_node *node)
{
	if (node) {
		avl_remove_tree(node->avl.left);
		avl_remove_tree(node->avl.right);
	}
}

static void stress_tree_avl(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	size_t i;
	struct tree_node *node, *head = NULL;

	for (node = data, i = 0; i < n; i++, node++) {
		bool taller = false;
		avl_insert(&head, node, &taller);
	}
	for (node = data, i = 0; i < n; i++, node++) {
		struct tree_node *find;

		find = avl_find(head, node);
		if (!find)
			pr_err("%s: avl tree node #%zd node found\n",
				args->name, i);
	}
	avl_remove_tree(head);
}

static void stress_tree_all(
	const args_t *args,
	const size_t n,
	struct tree_node *data)
{
	stress_tree_rb(args, n, data);
	stress_tree_splay(args, n, data);
	stress_tree_binary(args, n, data);
	stress_tree_avl(args, n, data);
}
#endif

/*
 * Table of tree stress methods
 */
static const stress_tree_method_info_t tree_methods[] = {
#if defined(HAVE_LIB_BSD) && !defined(__APPLE__)
	{ "all",	stress_tree_all },
	{ "avl",	stress_tree_avl },
	{ "binary",	stress_tree_binary },
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
 *  Rotate right a 64 bit value, compiler
 *  optimizes this down to a rotate and store
 */
static inline uint64_t ror64(const uint64_t val)
{
	register uint64_t tmp = val;
	register const uint64_t bit0 = (tmp & 1) << 63;

	tmp >>= 1;
        return (tmp | bit0);
}

/*
 *  stress_tree()
 *	stress tree
 */
int stress_tree(const args_t *args)
{
	uint64_t v, tree_size = DEFAULT_TREE_SIZE;
	struct tree_node *nodes, *node;
	size_t n, i, bit;
	struct sigaction old_action;
	int ret;
	stress_tree_method_info_t const *info = &tree_methods[0];

	(void)get_setting("tree-method", &info);

	if (!get_setting("tree-size", &tree_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			tree_size = MAX_TREE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			tree_size = MIN_TREE_SIZE;
	}
	n = (size_t)tree_size;

	if ((nodes = calloc(n, sizeof(struct tree_node))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_tree_handler, &old_action) < 0) {
		free(nodes);
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

	v = 0;
	for (node = nodes, i = 0, bit = 0; i < n; i++, node++) {
		if (!bit) {
			v = mwc64();
			bit = 1;
		} else {
			v ^= bit;
			bit <<= 1;
		}
		node->value = v;
		v = ror64(v);
	}

	do {
		uint64_t rnd;

		info->func(args, n, nodes);

		rnd = mwc64();
		for (node = nodes, i = 0; i < n; i++, node++)
			node->value = ror64(node->value ^ rnd);

		inc_counter(args);
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	free(nodes);

	return EXIT_SUCCESS;
}
#else
int stress_tree(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
