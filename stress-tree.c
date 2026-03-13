/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-pragma.h"
#include "core-signal.h"
#include "core-target-clones.h"

#include <math.h>

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

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
    defined(RB_REMOVE) &&	\
    !defined(__CYGWIN__)
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

#define MIN_TREE_SIZE		(1000)
#define MAX_TREE_SIZE		(25000000)	/* Must be uint32_t sized or less */
#define DEFAULT_TREE_SIZE	(250000)

typedef struct {
	double insert;		/* total insert duration */
	double find;		/* total find duration */
	double remove;		/* total remove duration */
	double count;		/* total nodes exercised */
} stress_tree_metrics_t;

typedef void (*stress_tree_func)(stress_args_t *args,
				 const uint32_t n,
				 void *data,
				 stress_tree_metrics_t *metrics,
				 int *rc);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_tree_func   func;	/* the tree method function */
} stress_tree_method_info_t;

static const stress_help_t help[] = {
	{ NULL,	"tree N",	 "start N workers that exercise tree structures" },
	{ NULL,	"tree-method M", "select tree method [ all | avl | binary | btree | rb | splay | treap ]" },
	{ NULL,	"tree-ops N",	 "stop after N bogo tree operations" },
	{ NULL,	"tree-size N",	 "N is the number of items in the tree" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

typedef struct binary_node {
	struct binary_node *left;
	struct binary_node *right;
	uint32_t value;
} binary_t;

#define EH	0
#define LH	1
#define RH	2

typedef struct avl_node {
	struct avl_node *left;
	struct avl_node *right;
	uint32_t value;
	uint8_t	bf;
} avl_t;

#define BTREE_M		(31)
#define BTREE_MIN	((BTREE_M >> 1) - 1)
#define BTREE_MAX	(BTREE_M - 1)

typedef struct btree_node {
	uint32_t value[BTREE_MAX + 1];
	struct btree_node *node[BTREE_MAX + 1];
	int count;
} btree_t;

typedef struct {
	uint32_t value;
} btree_value_t;

#if defined(HAVE_RB_TREE)
typedef struct rb_node {
	RB_ENTRY(rb_node)	rb;
	uint32_t value;
} rb_t;
#endif

#if defined(HAVE_SPLAY_TREE)
typedef struct splay_node {
	SPLAY_ENTRY(splay_node)	splay;
	uint32_t value;
} splay_t;
#endif

typedef struct treap_node {
	struct treap_node *left;
	struct treap_node *right;
	uint32_t value;
	uint32_t priority;
} treap_t;

union tree_node {
	avl_t avl;
	binary_t binary;
	btree_value_t btree;
#if defined(HAVE_RB_TREE)
	rb_t	rb;
#endif
#if defined(HAVE_SPLAY_TREE)
	splay_t	splay;
#endif
	treap_t treap;
};

static uint32_t stress_rndu32seed;

/*
 *  stress_rndu32_seed_set()
 *	set random seed
 */
static inline void ALWAYS_INLINE stress_rndu32_seed_set(const uint32_t seed)
{
	stress_rndu32seed = seed;
}

/*
 *  stress_rndu32()
 *	generate a relatively fast u32 random number
 */
static inline uint32_t ALWAYS_INLINE stress_rndu32(void)
{
	register uint32_t const a = 16843009;
	register uint32_t const b = 826366247;

	stress_rndu32seed *= a;
	stress_rndu32seed += b;

	return stress_rndu32seed;
}


#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_tree_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_tree_handler(int signum)
{
	stress_signal_siglongjmp_flag(signum, jmp_env, 1, &do_jmp);
}
#endif

#if defined(HAVE_RB_TREE)
static int OPTIMIZE3 rb_cmp_fwd(rb_t *n1, rb_t *n2)
{
	if (n1->value == n2->value)
		return 0;
	if (n1->value > n2->value)
		return 1;
	else
		return -1;
}
#endif

#if defined(HAVE_SPLAY_TREE)
static int OPTIMIZE3 splay_cmp_fwd(splay_t *n1, splay_t *n2)
{
	if (n1->value == n2->value)
		return 0;
	if (n1->value > n2->value)
		return 1;
	else
		return -1;
}
#endif

#if defined(HAVE_SPLAY_TREE)
#endif

#if defined(HAVE_RB_TREE)
static RB_HEAD(stress_rb_tree, rb_node) rb_root;
RB_PROTOTYPE(stress_rb_tree, rb_node, rb, rb_cmp_fwd);
RB_GENERATE(stress_rb_tree, rb_node, rb, rb_cmp_fwd)

static void OPTIMIZE3 stress_tree_rb(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	register rb_t *node, *next;
	register rb_t *nodes = (rb_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	RB_INIT(&rb_root);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();

		if (!RB_FIND(stress_rb_tree, &rb_root, node))
			RB_INSERT(stress_rb_tree, &rb_root, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!RB_FIND(stress_rb_tree, &rb_root, node))) {
			pr_fail("%s: rb tree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!RB_FIND(stress_rb_tree, &rb_root, &nodes[offset - i]))) {
				pr_fail("%s: rb tree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!RB_FIND(stress_rb_tree, &rb_root, &nodes[j]))) {
				pr_fail("%s: rb tree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
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
static SPLAY_HEAD(stress_splay_tree, splay_node) splay_root;
SPLAY_PROTOTYPE(stress_splay_tree, splay_node, splay, splay_cmp_fwd)
SPLAY_GENERATE(stress_splay_tree, splay_node, splay, splay_cmp_fwd)

static void OPTIMIZE3 stress_tree_splay(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	register splay_t *node, *next;
	register splay_t *nodes = (splay_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	SPLAY_INIT(&splay_root);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();

		if (!SPLAY_FIND(stress_splay_tree, &splay_root, node))
			SPLAY_INSERT(stress_splay_tree, &splay_root, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!SPLAY_FIND(stress_splay_tree, &splay_root, node))) {
			pr_fail("%s: splay tree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!SPLAY_FIND(stress_splay_tree, &splay_root, &nodes[offset - i]))) {
				pr_fail("%s: splay tree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!SPLAY_FIND(stress_splay_tree, &splay_root, &nodes[j]))) {
				pr_fail("%s: splay tree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
		}
	}
	t = stress_time_now();
	for (node = SPLAY_MIN(stress_splay_tree, &splay_root); node; node = next) {
		next = SPLAY_NEXT(stress_splay_tree, &splay_root, node);
		SPLAY_REMOVE(stress_splay_tree, &splay_root, node);
	}
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}
#endif

static treap_t * OPTIMIZE3 treap_split(
	treap_t *root,
	const treap_t *node,
	treap_t **remain)
{
	if (UNLIKELY(!root)) {
		*remain = NULL;
		return root;
	} else if (root->value < node->value) {
		root->right = treap_split(root->right, node, remain);
		return root;
	} else {
		*remain = root;
		return treap_split(root->left, node, &root->left);
	}
}

static treap_t * OPTIMIZE3 treap_join(
	treap_t * RESTRICT little,
	treap_t * RESTRICT big)
{
	if (!little) {
		return big;
	} else if (!big) {
		return little;
	} else if (little->priority < big->priority) {
		little->right = treap_join(little->right, big);
		return little;
	} else {
		big->left = treap_join(little, big->left);
		return big;
	}
}

static inline void ALWAYS_INLINE OPTIMIZE3 treap_insert(
	treap_t **root,
	treap_t *node)
{
	treap_t *big, *little;

	node->priority = stress_mwc32();
	little = treap_split(*root, node, &big);
	*root = treap_join(treap_join(little, node), big);
}

static treap_t * OPTIMIZE3 treap_find(
	treap_t *head,
	const treap_t *node)
{
	while (head) {
		if (node->value == head->value)
			return head;
		head = (node->value < head->value) ?
			head->left : head->right;
	}
	return head;
}

static void OPTIMIZE3 TARGET_CLONES treap_remove_tree(treap_t *node)
{
	if (node) {
		treap_remove_tree(node->left);
		treap_remove_tree(node->right);
		node->left = NULL;
		node->right = NULL;
	}
}

static void OPTIMIZE3 stress_tree_treap(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	treap_t *node, *head = NULL;
	treap_t *nodes = (treap_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();
		treap_insert(&head, node);
	}

	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!treap_find(head, node))) {
			pr_fail("%s: treap tree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!treap_find(head, &nodes[offset - i]))) {
				pr_fail("%s: treap tree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!treap_find(head, &nodes[j]))) {
				pr_fail("%s: treap tree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
		}
	}
	t = stress_time_now();
	treap_remove_tree(head);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static void OPTIMIZE3 binary_insert(
	binary_t **head,
	binary_t *node)
{
	while (*head) {
		head = (node->value <= (*head)->value) ?
			&(*head)->left : &(*head)->right;
	}
	*head = node;
}

static binary_t * OPTIMIZE3 binary_find(
	binary_t *head,
	const binary_t *node)
{
	while (head) {
		if (UNLIKELY(node->value == head->value))
			return head;
		head = (node->value <= head->value) ?
			head->left : head->right;
	}
	return NULL;
}

static void OPTIMIZE3 TARGET_CLONES binary_remove_tree(binary_t *node)
{
	if (node) {
		binary_remove_tree(node->left);
		binary_remove_tree(node->right);
		node->left = NULL;
		node->right = NULL;
	}
}

static void OPTIMIZE3 stress_tree_binary(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	binary_t *node, *head = NULL;
	binary_t *nodes = (binary_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();
		binary_insert(&head, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!binary_find(head, node))) {
			pr_fail("%s: binary tree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!binary_find(head, &nodes[offset - i]))) {
				pr_fail("%s: binary tree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!binary_find(head, &nodes[j]))) {
				pr_fail("%s: binary tree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
		}
	}
	t = stress_time_now();
	binary_remove_tree(head);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static bool OPTIMIZE3 avl_insert(
	avl_t **root,
	register avl_t *node)
{
	register avl_t *p, *q, *r = *root;
	register bool taller;

	if (UNLIKELY(r == NULL)) {
		*root = node;
		node->left = NULL;
		node->right = NULL;
		node->bf = EH;
		return true;
	}

	taller = false;
	if (node->value < r->value) {
		if (avl_insert(&r->left, node)) {
			switch (r->bf) {
			case EH:
				taller = true;
				r->bf = LH;
				break;
			case RH:
				r->bf = EH;
				break;
			case LH:
				/* Rebalance required */
				p = r->left;
				if (p->bf == LH) {
					/* Single rotation */
					r->left = p->right;
					p->right = r;
					p->bf = EH;
					r->bf = EH;
					*root = p;
				} else {
					/* Double rotation */
					q = p->right;
					r->left = q->right;
					q->right = r;
					p->right = q->left;
					q->left = p;

					/* Update balance factors */
					q->bf = EH;
					*root = q;
					switch (q->bf) {
					case RH:
						r->bf = EH;
						p->bf = LH;
						break;
					case LH:
						r->bf = RH;
						p->bf = EH;
						break;
					case EH:
						r->bf = EH;
						p->bf = EH;
						break;
					}
				}
				break;
			}
		}
	} else if (node->value > r->value) {
		if (avl_insert(&r->right, node)) {
			switch (r->bf) {
			case LH:
				r->bf = EH;
				break;
			case EH:
				taller = true;
				r->bf = RH;
				break;
			case RH:
				/* Rebalance required */
				p = r->right;
				if (p->bf == RH) {
					/* Single rotation */
					r->right = p->left;
					p->left = r;
					p->bf = EH;
					r->bf = EH;
					*root = p;
				} else {
					/* Double rotation */
					q = p->left;
					r->right = q->left;
					q->left = r;
					p->left = q->right;
					q->right = p;

					/* Update balance factors */
					q->bf = EH;
					*root = q;
					switch (q->bf) {
					case LH:
						r->bf = EH;
						p->bf = RH;
						break;
					case RH:
						r->bf = LH;
						p->bf = EH;
						break;
					case EH:
						r->bf = EH;
						p->bf = EH;
						break;
					}
				}
				break;
			}
		}
	}
	return taller;
}

static avl_t OPTIMIZE3 TARGET_CLONES *avl_find(
	avl_t *head,
	const avl_t *node)
{
	while (LIKELY(head != NULL)) {
		if (UNLIKELY(node->value == head->value))
			return head;
		head = (node->value <= head->value) ?
			head->left : head->right;
	}
	return NULL;
}

static void OPTIMIZE3 avl_remove_tree(avl_t *node)
{
	if (node) {
		avl_remove_tree(node->left);
		avl_remove_tree(node->right);
		node->left = NULL;
		node->right = NULL;
	}
}

static void OPTIMIZE3 stress_tree_avl(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	avl_t *node, *head = NULL;
	avl_t *nodes = (avl_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();
		avl_insert(&head, node);
	}
	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!avl_find(head, node))) {
			pr_fail("%s: avl tree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!avl_find(head, &nodes[offset - i]))) {
				pr_fail("%s: avl tree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!avl_find(head, &nodes[j]))) {
				pr_fail("%s: avl tree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
		}
	}
	t = stress_time_now();
	avl_remove_tree(head);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static inline void ALWAYS_INLINE btree_insert_node(
	const uint32_t value,
	const int pos,
	btree_t * node,
        btree_t * child)
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

static btree_t * OPTIMIZE3 btree_split_node(
	const uint32_t value,
	uint32_t *new_value,
	const int pos,
	btree_t *node,
	btree_t *child,
	bool *alloc_fail)
{
	btree_t *new_node;
	register int j;
	const int median = (pos > BTREE_MIN) ? BTREE_MIN + 1 : BTREE_MIN;

	new_node = (btree_t *)calloc(1, sizeof(*new_node));
	if (UNLIKELY(!new_node)) {
		*alloc_fail = true;
		return NULL;
	}

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

static btree_t * OPTIMIZE3 btree_insert_value(
	const uint32_t value,
	uint32_t *new_value,
	btree_t *node,
	bool *make_new_node,
	bool *alloc_fail)
{
	register int pos;
	btree_t *child;

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
			child = btree_split_node(*new_value, new_value, pos, node, child, alloc_fail);
			*make_new_node = true;
			return child;
		}
	}
	*make_new_node = false;
	return child;
}

static bool OPTIMIZE3 btree_insert(btree_t **root, const uint32_t value)
{
	bool flag;
	uint32_t new_value;
	btree_t *child;
	bool alloc_fail = false;

	child = btree_insert_value(value, &new_value, *root, &flag, &alloc_fail);
	if (flag) {
		btree_t *node;

		node = (btree_t *)calloc(1, sizeof(*node));
		if (UNLIKELY(!node))
			return true;
		node->count = 1;
		node->value[1] = new_value;
		node->node[0] = *root;
		node->node[1] = child;

		*root = node;
	}
	return alloc_fail;
}

static void OPTIMIZE3 btree_remove_tree(btree_t **node)
{
	int i;

	if (!*node)
		return;

	for (i = 0; i <= (*node)->count; i++)
		btree_remove_tree(&(*node)->node[i]);
	free(*node);
	*node = NULL;
}

static inline bool ALWAYS_INLINE OPTIMIZE3 btree_search(
	btree_t *node,
	const uint32_t value,
	int *pos)
{
	if (!node)
		return false;

	if (value < node->value[1]) {
		*pos = 0;
	} else {
		register int p = node->count;

		while ((value < node->value[p]) && (p > 1))
			p--;
		*pos = p;
		if (value == node->value[p])
			return true;
	}
	return btree_search(node->node[*pos], value, pos);
}

static inline bool ALWAYS_INLINE OPTIMIZE3 btree_find(btree_t *root, const uint32_t value)
{
	int pos;

	return btree_search(root, value, &pos);
}

static void OPTIMIZE3 stress_tree_btree(
	stress_args_t *args,
	const uint32_t n,
	void *data,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;
	btree_t *root = NULL;
	register btree_value_t *node;
	register btree_value_t *nodes = (btree_value_t *)data;
	double t;
	const uint32_t seed = stress_mwc32();

	(void)shim_memset(nodes, 0, sizeof(*nodes) * n);

	stress_rndu32_seed_set(seed);
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		node->value = stress_rndu32();

		if (UNLIKELY(btree_insert(&root, node->value) == true)) {
			pr_fail("%s: btree node #%" PRIu32 " allocation failure\n",
				args->name, i);
			btree_remove_tree(&root);
			return;
		}
	}
	metrics->insert += stress_time_now() - t;

	/* Mandatory forward tree check */
	t = stress_time_now();
PRAGMA_UNROLL_N(4)
	for (node = nodes, i = 0; i < n; i++, node++) {
		if (UNLIKELY(!btree_find(root, node->value))) {
			pr_fail("%s: btree node #%" PRIu32 " not found\n",
				args->name, i);
			*rc = EXIT_FAILURE;
		}
	}
	metrics->find += stress_time_now() - t;

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register uint32_t offset = n - 1;

		/* optional reverse find */
		for (i = 0; i < n; i++) {
			if (UNLIKELY(!btree_find(root, nodes[offset - i].value))) {
				pr_fail("%s: btree node #%" PRIu32 " not found\n",
					args->name, i);
				*rc = EXIT_FAILURE;
			}
		}
		/* optional random find */
		stress_rndu32_seed_set(seed);
PRAGMA_UNROLL_N(4)
		for (i = 0; i < n; i++) {
			register const uint32_t j = stress_mwc32modn(n);

			if (UNLIKELY(!btree_find(root, nodes[j].value))) {
				pr_fail("%s: btree node #%" PRIu32 " not found\n",
					args->name, j);
				*rc = EXIT_FAILURE;
			}
		}
	}
	t = stress_time_now();
	btree_remove_tree(&root);
	metrics->remove += stress_time_now() - t;
	metrics->count += (double)n;
}

static void stress_tree_all(
	stress_args_t *args,
	const uint32_t n,
	void *nodes,
	stress_tree_metrics_t *metrics,
	int *rc);

/*
 * Table of tree stress methods
 */
static const stress_tree_method_info_t stress_tree_methods[] = {
	{ "all",	stress_tree_all },
	{ "avl",	stress_tree_avl },
	{ "binary",	stress_tree_binary },
	{ "btree",	stress_tree_btree },
#if defined(HAVE_RB_TREE)
	{ "rb",		stress_tree_rb },
#endif
#if defined(HAVE_SPLAY_TREE)
	{ "splay",	stress_tree_splay },
#endif
	{ "treap",	stress_tree_treap },
};

static stress_tree_metrics_t stress_tree_metrics[SIZEOF_ARRAY(stress_tree_methods)];

static void stress_tree_all(
	stress_args_t *args,
	const uint32_t n,
	void *nodes,
	stress_tree_metrics_t *metrics,
	int *rc)
{
	register uint32_t i;

	(void)metrics;

	for (i = 1; i < SIZEOF_ARRAY(stress_tree_methods); i++) {
		stress_tree_methods[i].func(args, n, nodes, &stress_tree_metrics[i], rc);
	}
}

static const char *stress_tree_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_tree_methods)) ? stress_tree_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_tree_method, "tree-method", TYPE_ID_SIZE_T_METHOD, 0, 0, (void *)stress_tree_method },
	{ OPT_tree_size,   "tree-size",   TYPE_ID_UINT32, MIN_TREE_SIZE, MAX_TREE_SIZE, NULL },
	END_OPT,
};

/*
 *  stress_tree()
 *	stress tree
 */
static int stress_tree(stress_args_t *args)
{
	uint32_t tree_size = DEFAULT_TREE_SIZE;
	void *nodes;
	size_t n, i, j, tree_method = 0;
	int rc = EXIT_SUCCESS;
	stress_tree_func func;
	stress_tree_metrics_t *metrics;
	double mantissa;
	uint64_t exponent;
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	stress_signal_catch_sigill();

	for (i = 0; i < SIZEOF_ARRAY(stress_tree_metrics); i++) {
		stress_tree_metrics[i].insert = 0.0;
		stress_tree_metrics[i].find = 0.0;
		stress_tree_metrics[i].remove = 0.0;
		stress_tree_metrics[i].count = 0.0;
	}

	(void)stress_setting_get("tree-method", &tree_method);

	func = stress_tree_methods[tree_method].func;
	metrics = &stress_tree_metrics[tree_method];

	if (!stress_setting_get("tree-size", &tree_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			tree_size = MAX_TREE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			tree_size = MIN_TREE_SIZE;
	}
	n = (size_t)tree_size;
	nodes = calloc(n, sizeof(union tree_node));
	if (!nodes) {
		pr_inf_skip("%s: malloc failed allocating %zu tree nodes, "
			"skipping stressor\n", args->name, n);
		return EXIT_NO_RESOURCE;
	}

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_signal_restore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_signal_handler(args->name, SIGALRM, stress_tree_handler, &old_action) < 0) {
		free(nodes);
		return EXIT_FAILURE;
	}
#endif

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		func(args, tree_size, nodes, metrics, &rc);

		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_signal_restore(args->name, SIGALRM, &old_action);

tidy:
#endif
	mantissa = 1.0;
	exponent = 0;

	for (i = 0, j = 0; i < SIZEOF_ARRAY(stress_tree_metrics); i++) {
		double duration = stress_tree_metrics[i].insert +
				  stress_tree_metrics[i].find +
				  stress_tree_metrics[i].remove;

		if ((duration > 0.0) && (stress_tree_metrics[i].count > 0.0)) {
			const double rate = stress_tree_metrics[i].count / duration;
			char msg[64];
			double f;
			int e;

			(void)snprintf(msg, sizeof(msg), "%s tree operations per sec", stress_tree_methods[i].name);
			stress_metrics_set(args, msg, rate, STRESS_METRIC_HARMONIC_MEAN);

			f = frexp(rate, &e);
			mantissa *= f;
			exponent += e;
			j++;
		}
	}

	if (j > 0) {
		double geomean, inverse_n = 1.0 / (double)j;

		geomean = pow(mantissa, inverse_n) *
			  pow(2.0, (double)exponent * inverse_n);
		pr_dbg("%s: %.2f tree ops per second (geometric mean of per stressor tree op rates)\n",
			args->name, geomean);
	}
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	free(nodes);

	return rc;
}

const stressor_info_t stress_tree_info = {
	.stressor = stress_tree,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
