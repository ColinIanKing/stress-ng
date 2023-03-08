/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-pragma.h"
#include "core-target-clones.h"

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_LIB_BSD) &&	\
    !defined(__APPLE__)
/* BSD red-black tree */
#if defined(RB_HEAD) &&		\
    defined(RB_PROTOTYPE) &&	\
    defined(RB_GENERATE) &&	\
    defined(RB_ENTRY) &&	\
    defined(RB_INIT) &&		\
    defined(RB_FIND) &&		\
    defined(RB_INSERT) &&	\
    defined(RB_MIN) &&		\
    defined(RB_NEXT) &&		\
    defined(RB_REMOVE)
#define HAVE_RB_TREE
#endif

/* BSD splay tree */
#if defined(SPLAY_HEAD) &&	\
    defined(SPLAY_PROTOTYPE) &&	\
    defined(SPLAY_GENERATE) &&	\
    defined(SPLAY_ENTRY) &&	\
    defined(SPLAY_INIT) &&	\
    defined(SPLAY_FIND) &&	\
    defined(SPLAY_INSERT) &&	\
    defined(SPLAY_MIN) &&	\
    defined(SPLAY_NEXT) &&	\
    defined(SPLAY_REMOVE)
#define HAVE_SPLAY_TREE
#endif
#endif

#define MIN_TREE_SIZE		(1000)
#define MAX_TREE_SIZE		(25000000)	/* Must be uint32_t sized or less */
#define DEFAULT_TREE_SIZE	(250000)

struct tree_node;

typedef struct {
	double insert;		/* total insert duration */
	double find;		/* total find duration */
	double remove;		/* total remove duration */
	double count;		/* total nodes exercised */
} stress_tree_metrics_t;

typedef void (*stress_tree_func)(const stress_args_t *args,
				 const size_t n,
				 struct tree_node *data,
				 stress_tree_metrics_t *metrics);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_tree_func   func;	/* the tree method function */
} stress_tree_method_info_t;

static const stress_tree_method_info_t stress_tree_methods[];

static const stress_help_t help[] = {
	{ NULL,	"tree N",	 "start N workers that exercise tree structures" },
	{ NULL,	"tree-method M", "select tree method: all,avl,binary,btree,rb,splay" },
	{ NULL,	"tree-ops N",	 "stop after N bogo tree operations" },
	{ NULL,	"tree-size N",	 "N is the number of items in the tree" },
	{ NULL,	NULL,		 NULL }
};

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

#define BTREE_M		(31)
#define BTREE_MIN	((BTREE_M >> 1) - 1)
#define BTREE_MAX	(BTREE_M - 1)

typedef struct btree_node {
	uint32_t value[BTREE_MAX + 1];
	struct btree_node *node[BTREE_MAX + 1];
	int count;
} btree_node_t;

struct tree_node {
	uint32_t value;
	union {
#if defined(HAVE_RB_TREE)
		RB_ENTRY(tree_node)	rb;
#endif
#if defined(HAVE_SPLAY_TREE)
		SPLAY_ENTRY(tree_node)	splay;
#endif
		struct binary_node	binary;
		struct avl_node		avl;
	} u;
};

/*
 *  stress_set_tree_size()
 *	set tree size
 */
static int stress_set_tree_size(const char *opt)
{
	uint64_t tree_size;

	tree_size = stress_get_uint64(opt);
	stress_check_range("tree-size", tree_size,
		MIN_TREE_SIZE, MAX_TREE_SIZE);
	return stress_set_setting("tree-size", TYPE_ID_UINT64, &tree_size);
}

/*
 *  stress_tree_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_tree_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

#if defined(HAVE_RB_TREE) ||	\
    defined(HAVE_SPLAY_TREE)
static int OPTIMIZE3 tree_node_cmp_fwd(struct tree_node *n1, struct tree_node *n2)
{
	if (n1->value == n2->value)
		return 0;
	if (n1->value > n2->value)
		return 1;
	else
		return -1;
}
#endif

#if defined(HAVE_RB_TREE)
static RB_HEAD(stress_rb_tree, tree_node) rb_root;
RB_PROTOTYPE(stress_rb_tree, tree_node, u.rb, tree_node_cmp_fwd);
RB_GENERATE(stress_rb_tree, tree_node, u.rb, tree_node_cmp_fwd);

static void OPTIMIZE3 stress_tree_rb(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;
	register struct tree_node *node, *next;
	struct tree_node *find;
	double t;

	RB_INIT(&rb_root);

	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		register struct tree_node *res;

		res = RB_FIND(stress_rb_tree, &rb_root, node);
		if (!res)
			RB_INSERT(stress_rb_tree, &rb_root, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Manditory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		find = RB_FIND(stress_rb_tree, &rb_root, node);
		if (!find)
			pr_fail("%s: rb tree node #%zd not found\n",
				args->name, i);
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		/* optional reverse find */
		for (node = &nodes[n - 1], i = n - 1; node >= nodes; node--, i--) {
			find = RB_FIND(stress_rb_tree, &rb_root, node);
			if (!find)
				pr_fail("%s: rb tree node #%zd not found\n",
					args->name, i);
		}
		/* optional random find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			const size_t j = stress_mwc32modn(n);

			find = RB_FIND(stress_rb_tree, &rb_root, &nodes[j]);
			if (!find)
				pr_fail("%s: rb tree node #%zd not found\n",
					args->name, j);
		}
	}

	t = stress_time_now();
	for (node = RB_MIN(stress_rb_tree, &rb_root); node; node = next) {
		next = RB_NEXT(stress_rb_tree, &rb_root, node);
		RB_REMOVE(stress_rb_tree, &rb_root, node);
	}
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}
#endif

#if defined(HAVE_SPLAY_TREE)
static SPLAY_HEAD(stress_splay_tree, tree_node) splay_root;
SPLAY_PROTOTYPE(stress_splay_tree, tree_node, u.splay, tree_node_cmp_fwd);
SPLAY_GENERATE(stress_splay_tree, tree_node, u.splay, tree_node_cmp_fwd);

static void OPTIMIZE3 stress_tree_splay(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;
	register struct tree_node *node, *next;
	struct tree_node *find;
	double t;

	SPLAY_INIT(&splay_root);

	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		register struct tree_node *res;

		res = SPLAY_FIND(stress_splay_tree, &splay_root, node);
		if (!res)
			SPLAY_INSERT(stress_splay_tree, &splay_root, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Manditory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		find = SPLAY_FIND(stress_splay_tree, &splay_root, node);
		if (!find)
			pr_fail("%s: splay tree node #%zd not found\n",
				args->name, i);
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		/* optional reverse find */
		for (node = &nodes[n - 1], i = n - 1; node >= nodes; node--, i--) {
			find = SPLAY_FIND(stress_splay_tree, &splay_root, node);
			if (!find)
				pr_fail("%s: splay tree node #%zd not found\n",
					args->name, i);
		}
		/* optional random find */
		for (i = 0; i < n; i++) {
			const size_t j = stress_mwc32modn(n);

			find = SPLAY_FIND(stress_splay_tree, &splay_root, &nodes[j]);
			if (!find)
				pr_fail("%s: splay tree node #%zd not found\n",
					args->name, j);
		}
	}
	t = stress_time_now();
	for (node = SPLAY_MIN(stress_splay_tree, &splay_root); node; node = next) {
		next = SPLAY_NEXT(stress_splay_tree, &splay_root, node);
		SPLAY_REMOVE(stress_splay_tree, &splay_root, node);
		(void)memset(&node->u.splay, 0, sizeof(node->u.splay));
	}
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}
#endif

static void OPTIMIZE3 TARGET_CLONES binary_insert(
	struct tree_node **head,
	struct tree_node *node)
{
	while (*head) {
		head = (node->value <= (*head)->value) ?
			&(*head)->u.binary.left :
			&(*head)->u.binary.right;
	}
	*head = node;
}

static struct tree_node * OPTIMIZE3 TARGET_CLONES binary_find(
	struct tree_node *head,
	struct tree_node *node)
{
	while (head) {
		if (node->value == head->value)
			return head;
		head = (node->value <= head->value) ?
				head->u.binary.left :
				head->u.binary.right;
	}
	return NULL;
}

static void OPTIMIZE3 TARGET_CLONES binary_remove_tree(struct tree_node *node)
{
	if (node) {
		binary_remove_tree(node->u.binary.left);
		binary_remove_tree(node->u.binary.right);
		node->u.binary.left = NULL;
		node->u.binary.right = NULL;
	}
}

static void OPTIMIZE3 stress_tree_binary(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;
	struct tree_node *node, *head = NULL;
	struct tree_node *find;
	double t;

	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		binary_insert(&head, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Manditory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		find = binary_find(head, node);
		if (!find)
			pr_fail("%s: binary tree node #%zd not found\n",
				args->name, i);
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		/* optional reverse find */
		for (node = &nodes[n - 1], i = n - 1; node >= nodes; node--, i--) {
			find = binary_find(head, node);
			if (!find)
				pr_fail("%s: binary tree node #%zd not found\n",
					args->name, i);
		}
		/* optional random find */
		for (i = 0; i < n; i++) {
			const size_t j = stress_mwc32modn(n);

			find = binary_find(head, &nodes[j]);
			if (!find)
				pr_fail("%s: binary tree node #%zd not found\n",
					args->name, j);
		}
	}
	t = stress_time_now();
	binary_remove_tree(head);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static void OPTIMIZE3 avl_insert(
	struct tree_node **root,
	struct tree_node *node,
	bool *taller)
{
	bool sub_taller = false;
	register struct tree_node *p, *q;

	if (!*root) {
		*root = node;
		(*root)->u.avl.left = NULL;
		(*root)->u.avl.right = NULL;
		(*root)->u.avl.bf = EH;
		*taller = true;
	} else {
		if (node->value < (*root)->value) {
			avl_insert(&(*root)->u.avl.left, node, &sub_taller);
			if (sub_taller) {
				switch ((*root)->u.avl.bf) {
				case EH:
					(*root)->u.avl.bf = LH;
					*taller = true;
					break;
				case RH:
					(*root)->u.avl.bf = EH;
					*taller = false;
					break;
				case LH:
					/* Rebalance required */
					p = (*root)->u.avl.left;
					if (p->u.avl.bf == LH) {
						/* Single rotation */
						(*root)->u.avl.left = p->u.avl.right;
						p->u.avl.right = *root;
						p->u.avl.bf = EH;
						(*root)->u.avl.bf = EH;
						*root = p;
					} else {
						/* Double rotation */
						q = p->u.avl.right;
						(*root)->u.avl.left = q->u.avl.right;
						q->u.avl.right = *root;
						p->u.avl.right = q->u.avl.left;
						q->u.avl.left = p;

						/* Update balance factors */
						switch (q->u.avl.bf) {
						case RH:
							(*root)->u.avl.bf = EH;
							p->u.avl.bf = LH;
							break;
						case LH:
							(*root)->u.avl.bf = RH;
							p->u.avl.bf = EH;
							break;
						case EH:
							(*root)->u.avl.bf = EH;
							p->u.avl.bf = EH;
							break;
						}
						q->u.avl.bf = EH;
						*root = q;
					}
					*taller = false;
					break;
				}
			}
		} else if (node->value > (*root)->value) {
			avl_insert(&(*root)->u.avl.right, node, &sub_taller);
			if (sub_taller) {
				switch ((*root)->u.avl.bf) {
				case LH:
					(*root)->u.avl.bf = EH;
					*taller = false;
					break;
				case EH:
					(*root)->u.avl.bf = RH;
					*taller = true;
					break;
				case RH:
					/* Rebalance required */
					p = (*root)->u.avl.right;
					if (p->u.avl.bf == RH) {
						/* Single rotation */
						(*root)->u.avl.right = p->u.avl.left;
						p->u.avl.left = *root;
						p->u.avl.bf = EH;
						(*root)->u.avl.bf = EH;
						*root = p;
					} else {
						/* Double rotation */
						q = p->u.avl.left;
						(*root)->u.avl.right = q->u.avl.left;
						q->u.avl.left = *root;
						p->u.avl.left = q->u.avl.right;
						q->u.avl.right = p;

						/* Update balance factors */
						switch (q->u.avl.bf) {
						case LH:
							(*root)->u.avl.bf = EH;
							p->u.avl.bf = RH;
							break;
						case RH:
							(*root)->u.avl.bf = LH;
							p->u.avl.bf = EH;
							break;
						case EH:
							(*root)->u.avl.bf = EH;
							p->u.avl.bf = EH;
							break;
						}
						q->u.avl.bf = EH;
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

static struct tree_node OPTIMIZE3 TARGET_CLONES *avl_find(
	struct tree_node *head,
	struct tree_node *node)
{
	while (LIKELY(head != NULL)) {
		if (node->value == head->value)
			return head;
		head = (node->value <= head->value) ?
				head->u.avl.left :
				head->u.avl.right;
	}
	return NULL;
}

static void OPTIMIZE3 avl_remove_tree(struct tree_node *node)
{
	if (node) {
		avl_remove_tree(node->u.avl.left);
		avl_remove_tree(node->u.avl.right);
		node->u.avl.left = NULL;
		node->u.avl.right = NULL;
	}
}

static void OPTIMIZE3 stress_tree_avl(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;
	struct tree_node *node, *head = NULL;
	struct tree_node *find;
	double t;

	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		bool taller = false;
		avl_insert(&head, node, &taller);
	}
	metrics->insert += stress_time_now() - t;

	/* Manditory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		find = avl_find(head, node);
		if (!find)
			pr_fail("%s: avl tree node #%zd not found\n",
				args->name, i);
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		/* optional reverse find */
		for (node = &nodes[n - 1], i = n - 1; node >= nodes; node--, i--) {
			find = avl_find(head, node);
			if (!find)
				pr_fail("%s: avl tree node #%zd not found\n",
					args->name, i);
		}
		/* optional random find */
		for (i = 0; i < n; i++) {
			const size_t j = stress_mwc32modn(n);

			find = avl_find(head, &nodes[j]);
			if (!find)
				pr_fail("%s: avl tree node #%zd not found\n",
					args->name, j);
		}
	}
	t = stress_time_now();
	avl_remove_tree(head);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static void OPTIMIZE3 TARGET_CLONES btree_insert_node(
	const uint32_t value,
	const int pos,
	btree_node_t * node,
        btree_node_t * child)
{
	register int j = node->count;

	while (j > pos) {
		node->value[j + 1] = node->value[j];
		node->node[j + 1] = node->node[j];
		j--;
	}
	node->value[j + 1] = value;
	node->node[j + 1] = child;
	node->count++;
}

static btree_node_t * OPTIMIZE3 btree_split_node(
	const uint32_t value,
	uint32_t *new_value,
	const int pos,
	btree_node_t *node,
	btree_node_t *child)
{
	btree_node_t *new_node;
	register int j;
	const int median = (pos > BTREE_MIN) ? BTREE_MIN + 1 : BTREE_MIN;

	new_node = (btree_node_t *)calloc(1, sizeof(*new_node));
	if (UNLIKELY(!new_node))
		return NULL;

	j = median + 1;
	while (j <= BTREE_MAX) {
		new_node->value[j - median] = node->value[j];
		new_node->node[j - median] = node->node[j];
		j++;
	}
	node->count = median;
	new_node->count = BTREE_MAX - median;

	if (pos <= BTREE_MIN) {
		btree_insert_node(value, pos, node, child);
	} else {
		btree_insert_node(value, pos - median, new_node, child);
	}
	*new_value = node->value[node->count];
	new_node->node[0] = node->node[node->count];
	node->count--;

	return new_node;
}

static btree_node_t * OPTIMIZE3 btree_insert_value(
	const uint32_t value,
	uint32_t *new_value,
	btree_node_t *node,
	bool *make_new_node,
	bool *alloc_fail)
{
	register int pos;
	btree_node_t *child;

	if (UNLIKELY(!node)) {
		*new_value = value;
		*make_new_node = true;
		return NULL;
	}

	if (value < node->value[1]) {
		pos = 0;
	} else {
		pos = node->count;
		while ((value < node->value[pos]) && (pos > 1))
			pos--;
		if (UNLIKELY(value == node->value[pos])) {
			*make_new_node = false;
			return node;
		}
	}
	child = btree_insert_value(value, new_value, node->node[pos], make_new_node, alloc_fail);
	if (*make_new_node) {
		if (node->count < BTREE_MAX) {
			btree_insert_node(*new_value, pos, node, child);
		} else {
			child = btree_split_node(*new_value, new_value, pos, node, child);
			*make_new_node = true;
			return child;
		}
	}
	*make_new_node = false;
	return child;
}

static bool OPTIMIZE3 btree_insert(btree_node_t **root, const uint32_t value)
{
	bool flag;
	uint32_t new_value;
	btree_node_t *child;
	bool alloc_fail = false;

	child = btree_insert_value(value, &new_value, *root, &flag, &alloc_fail);
	if (flag) {
		btree_node_t *node;

		node = (btree_node_t *)calloc(1, sizeof(*node));
		if (UNLIKELY(!node))
			return false;
		node->count = 1;
		node->value[1] = new_value;
		node->node[0] = *root;
		node->node[1] = child;

		*root = node;
	}
	return alloc_fail;
}

static void OPTIMIZE3 btree_remove_tree(btree_node_t **node)
{
	int i;

	if (!*node)
		return;

PRAGMA_UNROLL_N(4)
	for (i = 0; i <= (*node)->count; i++) {
		btree_remove_tree(&(*node)->node[i]);
		free((*node)->node[i]);
		(*node)->node[i] = NULL;
	}
	free(*node);
	*node = NULL;
}

static inline bool OPTIMIZE3 btree_search(
	btree_node_t *node,
	const uint32_t value,
	int *pos)
{
	if (!node)
		return false;

	if (value < node->value[1]) {
		*pos = 0;
	} else {
		register int p;

		p = node->count;
		while ((value < node->value[p]) && (p > 1))
			p--;
		*pos = p;
		if (value == node->value[*pos])
			return true;
	}
	return btree_search(node->node[*pos], value, pos);
}

static inline bool OPTIMIZE3 btree_find(btree_node_t *root, const uint32_t value)
{
	int pos;

	return btree_search(root, value, &pos);
}

static void stress_tree_btree(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;
	struct tree_node *node;
	btree_node_t *root = NULL;
	bool find;
	double t;

	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++)
		btree_insert(&root, node->value);
	metrics->insert += stress_time_now() - t;

	/* Manditory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		find = btree_find(root, node->value);
		if (!find)
			pr_fail("%s: btree node #%zd not found\n",
				args->name, i);
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		/* optional reverse find */
		for (node = &nodes[n - 1], i = n - 1; node >= nodes; node--, i--) {
			find = btree_find(root, node->value);
			if (!find)
				pr_fail("%s: btree node #%zd not found\n",
					args->name, i);
		}
		/* optional random find */
		for (i = 0; i < n; i++) {
			const size_t j = stress_mwc32modn(n);

			find = btree_find(root, nodes[j].value);
			if (!find)
				pr_fail("%s: btree node #%zd not found\n",
					args->name, j);
		}
	}
	t = stress_time_now();
	btree_remove_tree(&root);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static void stress_tree_all(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics);

/*
 * Table of tree stress methods
 */
static const stress_tree_method_info_t stress_tree_methods[] = {
	{ "all",	stress_tree_all },
	{ "avl",	stress_tree_avl },
	{ "binary",	stress_tree_binary },
#if defined(HAVE_RB_TREE)
	{ "rb",		stress_tree_rb },
#endif
#if defined(HAVE_SPLAY_TREE)
	{ "splay",	stress_tree_splay },
#endif
	{ "btree",	stress_tree_btree },
};

static stress_tree_metrics_t stress_tree_metrics[SIZEOF_ARRAY(stress_tree_methods)];

static void stress_tree_all(
	const stress_args_t *args,
	const size_t n,
	struct tree_node *nodes,
	stress_tree_metrics_t *metrics)
{
	size_t i;

	(void)metrics;

	for (i = 1; i < SIZEOF_ARRAY(stress_tree_methods); i++) {
		stress_tree_methods[i].func(args, n, nodes, &stress_tree_metrics[i]);
	}
}

/*
 *  stress_set_tree_method()
 *	set the default funccal stress method
 */
static int stress_set_tree_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_tree_methods); i++) {
		if (!strcmp(stress_tree_methods[i].name, name)) {
			stress_set_setting("tree-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "tree-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_tree_methods); i++) {
		(void)fprintf(stderr, " %s", stress_tree_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_tree_method,	stress_set_tree_method },
	{ OPT_tree_size,	stress_set_tree_size },
	{ 0,			NULL }
};

static void OPTIMIZE3 TARGET_CLONES stress_tree_shuffle(struct tree_node *nodes, const size_t n)
{
	register uint32_t const a = 16843009;
	register uint32_t const c = 826366247;
	register uint32_t seed = 99; //stress_mwc32();
	register size_t i;

PRAGMA_UNROLL_N(4)
	for (i = 0; i < n; i++) {
		register uint32_t j;
		register uint32_t tmp;

		j = seed % n;
		seed *= a;
		seed += c;
		tmp = nodes[i].value;
		nodes[i].value = nodes[j].value;
		nodes[j].value = tmp;
	}
}

/*
 *  stress_tree()
 *	stress tree
 */
static int stress_tree(const stress_args_t *args)
{
	uint64_t tree_size = DEFAULT_TREE_SIZE;
	struct tree_node *nodes;
	size_t n, i, j, tree_method = 0;
	struct sigaction old_action;
	int ret;
	stress_tree_func func;
	stress_tree_metrics_t *metrics;

	for (i = 0; i < SIZEOF_ARRAY(stress_tree_metrics); i++) {
		stress_tree_metrics[i].insert = 0.0;
		stress_tree_metrics[i].find = 0.0;
		stress_tree_metrics[i].remove = 0.0;
		stress_tree_metrics[i].count = 0.0;
	}

	(void)stress_get_setting("tree-method", &tree_method);

	func = stress_tree_methods[tree_method].func;
	metrics = &stress_tree_metrics[tree_method];

	if (!stress_get_setting("tree-size", &tree_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			tree_size = MAX_TREE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			tree_size = MIN_TREE_SIZE;
	}
	n = (size_t)tree_size;
	nodes = calloc(n, sizeof(*nodes));
	if (!nodes) {
		pr_inf_skip("%s: malloc failed allocating %zd tree nodes, "
			"skipping stressor\n", args->name, n);
		return EXIT_NO_RESOURCE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_tree_handler, &old_action) < 0) {
		free(nodes);
		return EXIT_FAILURE;
	}


	for (i = 0; i < n; i++)
		nodes[i].value = (uint32_t)i;
	stress_tree_shuffle(nodes, n);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		func(args, n, nodes, metrics);
		stress_tree_shuffle(nodes, n);

		inc_counter(args);
	} while (keep_stressing(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);

tidy:
	for (i = 0, j = 0; i < SIZEOF_ARRAY(stress_tree_metrics); i++) {
		double duration = stress_tree_metrics[i].insert +
				  stress_tree_metrics[i].find +
				  stress_tree_metrics[i].remove;

		if ((duration > 0.0) && (stress_tree_metrics[i].count > 0.0)) {
			double rate = stress_tree_metrics[i].count / duration;
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s tree operations per sec", stress_tree_methods[i].name);
			stress_metrics_set(args, j, msg, rate);
			j++;
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(nodes);

	return EXIT_SUCCESS;
}

stressor_info_t stress_tree_info = {
	.stressor = stress_tree,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
