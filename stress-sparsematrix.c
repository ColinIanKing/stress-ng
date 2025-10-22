/*
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
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-pragma.h"
#include "core-prime.h"
#include "math.h"

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_JUDY_H)
#include <Judy.h>
#endif

/* Number of items in sparse matrix */
#define MIN_SPARSEMATRIX_ITEMS		(100)
#define MAX_SPARSEMATRIX_ITEMS		(10000000)
#define DEFAULT_SPARSEMATRIX_ITEMS	(10000)

/* Sparse matrix X x Y size */
#define MIN_SPARSEMATRIX_SIZE		(10)
#define MAX_SPARSEMATRIX_SIZE		(10000000)
#define DEFAULT_SPARSEMATRIX_SIZE	(500)

#if defined(CIRCLEQ_ENTRY) && 		\
    defined(CIRCLEQ_HEAD) &&		\
    defined(CIRCLEQ_INIT) &&		\
    defined(CIRCLEQ_INSERT_TAIL) &&	\
    defined(CIRCLEQ_FOREACH) &&		\
    defined(CIRCLEQ_FIRST) &&		\
    defined(CIRCLEQ_REMOVE)
#define HAVE_SYS_QUEUE_CIRCLEQ	(1)
#endif

#if defined(HAVE_LIB_BSD) &&	\
    !defined(__APPLE__) &&	\
    !defined(__CYGWIN__)
#define HAVE_RB_TREE	(1)
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

#if defined(HAVE_JUDY_H) && 	\
    defined(HAVE_LIB_JUDY) &&	\
    (ULONG_MAX > 0xffffffff)
#define HAVE_JUDY	(1)
#endif

#define SPARSE_TEST_OK		(0)
#define SPARSE_TEST_FAILED	(-1)
#define SPARSE_TEST_ENOMEM	(-2)

typedef void * (*func_create)(const uint64_t n, const uint32_t x, const uint32_t y);
typedef void (*func_destroy)(void *handle, size_t *objmem);
typedef int (*func_put)(void *handle, const uint32_t x, const uint32_t y, const uint32_t value);
typedef void (*func_del)(void *handle, const uint32_t x, const uint32_t y);
typedef uint32_t (*func_get)(void *handle, const uint32_t x, const uint32_t y);

typedef struct {
	const char              *name;  /* human readable form of sparse method */
	const func_create	create;	/* create sparse matrix */
	const func_destroy	destroy;/* destroy sparse matrix */
	const func_put		put;	/* put a value to the matrix */
	const func_del		del;	/* mark a value as deleted in the matrix */
	const func_get		get;	/* get a value from the matrix */
} stress_sparsematrix_method_info_t;

static const stress_sparsematrix_method_info_t sparsematrix_methods[];

static const stress_help_t help[] = {
	{ NULL,	"sparsematrix N",	 "start N workers that exercise a sparse matrix" },
	{ NULL,	"sparsematrix-items N",	 "N is the number of items in the spare matrix" },
	{ NULL,	"sparsematrix-method M", "select storage method: all, hash, hashjudy, judy, list, mmap, qhash, rb, splay" },
	{ NULL,	"sparsematrix-ops N",	 "stop after N bogo sparse matrix operations" },
	{ NULL,	"sparsematrix-size N",	 "M is the width and height X x Y of the matrix" },
	{ NULL,	NULL,		 	 NULL }
};

#if defined(HAVE_RB_TREE) &&	\
    defined(RB_ENTRY)
typedef struct sparse_rb {
	uint64_t xy;		/* x,y matrix position */
	uint32_t value;		/* value in matrix x,y */
	RB_ENTRY(sparse_rb) rb;	/* red-black tree node entry */
} sparse_rb_t;
#else
UNEXPECTED
#endif

#if defined(HAVE_SPLAY_TREE) &&	\
    defined(SPLAY_ENTRY)
typedef struct sparse_splay {
	uint64_t xy;		/* x,y matrix position */
	uint32_t value;		/* value in matrix x,y */
	SPLAY_ENTRY(sparse_splay) splay; /* splay tree node entry */
} sparse_splay_t;
#else
UNEXPECTED
#endif

typedef struct sparse_hash_node {
	struct sparse_hash_node *next;
	uint64_t xy;		/* x,y matrix position */
	uint32_t value;		/* value in matrix x,y */
} sparse_hash_node_t;

typedef struct sparse_hash_table {
	uint64_t n;		/* size of hash table */
	sparse_hash_node_t **table;
} sparse_hash_table_t;

#if defined(HAVE_JUDY_H)
typedef struct sparse_hashjudy_table {
	uint64_t n;		/* size of hash table */
	Pvoid_t *hash_table;	/* hash table of Judy arrays */
} sparse_hashjudy_table_t;
#endif

typedef struct sparse_qhash_node {
	struct sparse_qhash_node *next;
	uint64_t xy;		/* x,y matrix position */
	uint32_t value;		/* value in matrix x,y */
} sparse_qhash_node_t;

typedef struct sparse_qhash_table {
	uint64_t n;		/* size of hash table */
	uint64_t n_nodes;	/* number of nodes */
	sparse_qhash_node_t **table;
	sparse_qhash_node_t *nodes;
	size_t idx;
} sparse_qhash_table_t;

#if defined(HAVE_SYS_QUEUE_CIRCLEQ) &&	\
    defined(CIRCLEQ_ENTRY)
typedef struct sparse_x_list_node {
	CIRCLEQ_ENTRY(sparse_x_list_node) sparse_x_list;
	uint32_t value;		/* value in matrix x,y */
	uint32_t x;		/* x matrix position */
} sparse_x_list_node_t;

CIRCLEQ_HEAD(sparse_x_list, sparse_x_list_node);

typedef struct sparse_x_list sparse_x_list_t;

typedef struct sparse_y_list_node {
	CIRCLEQ_ENTRY(sparse_y_list_node) sparse_y_list;
	sparse_x_list_t x_head;	/* list items on x axis */
	uint32_t y;		/* y axis value */
} sparse_y_list_node_t;

CIRCLEQ_HEAD(sparse_y_list, sparse_y_list_node);

typedef struct sparse_y_list sparse_y_list_t;
#else
UNEXPECTED
#endif

typedef struct {
	void *mmap;
	uint64_t mmap_size;
	uint32_t x;
	uint32_t y;
} sparse_mmap_t;

typedef struct {
	size_t	max_objmem;	/* Object memory allocation estimate */
	double	put_duration;	/* Total put duration time, seconds */
	double	get_duration;	/* Total get duration time, seconds */
	uint64_t put_ops;	/* Total put object op count */
	uint64_t get_ops;	/* Total put object op count */
	bool	skip_no_mem;	/* True if can't allocate memory */
} test_info_t;

static inline uint32_t value_map(const uint32_t x, register uint32_t y)
{
	return x ^ ~y;
}

/*
 *  hash_create()
 *	create a hash table based sparse matrix
 */
static void *hash_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	sparse_hash_table_t *table;
	uint64_t n_prime = (uint64_t)stress_get_next_prime64(n);

	(void)x;
	(void)y;

	table = (sparse_hash_table_t *)calloc(1, sizeof(*table));
	if (UNLIKELY(!table))
		return NULL;

	table->table = (sparse_hash_node_t **)calloc((size_t)n_prime, sizeof(sparse_hash_node_t *));
	if (UNLIKELY(!table->table)) {
		free(table);
		return NULL;
	}
	table->n = n_prime;

	return (void *)table;
}

/*
 *  hash_destroy()
 *	destroy a hash table based sparse matrix
 */
static void hash_destroy(void *handle, size_t *objmem)
{
	size_t i, n;
	sparse_hash_table_t *table = (sparse_hash_table_t *)handle;

	*objmem = 0;
	if (!handle)
		return;

	n = table->n;
	for (i = 0; i < n; i++) {
		sparse_hash_node_t *node = table->table[i];

		while (node) {
			sparse_hash_node_t *next = node->next;

			free(node);
			*objmem += sizeof(*node);
			node = next;
		}
	}
	*objmem += sizeof(*table) +
		   sizeof(*table->table) * table->n;
	free(table->table);
	table->n = 0;
	table->table = 0;
	free(table);
}

/*
 *  hash_put()
 *	put a value into a hash based sparse matrix
 */
static int OPTIMIZE3 hash_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_hash_node_t *node;
	sparse_hash_table_t *table = (sparse_hash_table_t *)handle;
	size_t hash;
	const uint64_t xy = ((uint64_t)x << 32) | y;

	if (UNLIKELY(!table))
		return -1;

	hash = (((size_t)x << 3) ^ y) % table->n;

	/* Find and put */
	for (node = table->table[hash]; node; node = node->next) {
		if (node->xy == xy) {
			node->value = value;
			return 0;
		}
	}

	/* Not found, allocate and add */
	node = (sparse_hash_node_t *)malloc(sizeof(*node));
	if (UNLIKELY(!node))
		return -1;
	node->value = value;
	node->next = table->table[hash];
	node->xy = xy;
	table->table[hash] = node;
	return 0;
}

/*
 *  hash_get_node()
 *	find the hash table node of a (x,y) value in a hash table
 */
static sparse_hash_node_t OPTIMIZE3 *hash_get_node(void *handle, const uint32_t x, const uint32_t y)
{
	const sparse_hash_table_t *table = (sparse_hash_table_t *)handle;
	sparse_hash_node_t *node;
	size_t hash;
	const uint64_t xy = ((uint64_t)x << 32) | y;

	if (UNLIKELY(!table))
		return NULL;
	hash = (((size_t)x << 3) ^ y) % table->n;

	for (node = table->table[hash]; node; node = node->next) {
		if (node->xy == xy)
			return node;
	}
	return NULL;
}

/*
 *  hash_get()
 *	get the (x,y) value in hash table based sparse matrix
 */
static uint32_t OPTIMIZE3 hash_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hash_node_t *node = hash_get_node(handle, x, y);

	return node ? node->value : 0;
}

/*
 *  hash_del()
 *	zero the (x,y) value in sparse hash table
 */
static void OPTIMIZE3 hash_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hash_node_t *node = hash_get_node(handle, x, y);

	if (LIKELY(node != NULL))
		node->value = 0;
}

/*
 *  qhash_create()
 *	create a hash table based sparse matrix
 */
static void *qhash_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	sparse_qhash_table_t *table;
	const uint64_t n_prime = stress_get_next_prime64(n);

	(void)x;
	(void)y;

	table = (sparse_qhash_table_t *)calloc(1, sizeof(*table));
	if (UNLIKELY(!table))
		return NULL;

	table->table = (sparse_qhash_node_t **)calloc((size_t)n_prime, sizeof(sparse_qhash_node_t *));
	if (UNLIKELY(!table->table)) {
		free(table);
		return NULL;
	}
	table->nodes = (sparse_qhash_node_t *)calloc((size_t)n, sizeof(sparse_qhash_node_t));
	if (UNLIKELY(!table->nodes)) {
		free(table->table);
		free(table);
		return NULL;
	}
	(void)shim_memset(table->nodes, 0xff, (size_t)n * sizeof(sparse_qhash_node_t));
	table->n_nodes = n;
	table->n = n_prime;
	table->idx = 0;

	return (void *)table;
}

/*
 *  qhash_destroy()
 *	destroy a hash table based sparse matrix
 */
static void qhash_destroy(void *handle, size_t *objmem)
{
	sparse_qhash_table_t *table = (sparse_qhash_table_t *)handle;

	*objmem = 0;
	if (UNLIKELY(!handle))
		return;

	*objmem = sizeof(*table) +
		  (sizeof(*table->nodes) * table->n_nodes) +
		  (sizeof(*table->table) * table->n);

	free(table->nodes);
	table->nodes = NULL;

	free(table->table);
	table->table = NULL;
	table->n = 0;
	table->n_nodes = 0;
	table->idx = 0;

	free(table);
	table = NULL;
}

/*
 *  qhash_put()
 *	put a value into a hash based sparse matrix
 */
static int OPTIMIZE3 qhash_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_qhash_node_t *node;
	sparse_qhash_table_t *table = (sparse_qhash_table_t *)handle;
	size_t hash;
	const uint64_t xy = ((uint64_t)x << 32) | y;

	if (UNLIKELY(!table))
		return -1;
	if (UNLIKELY(table->idx >= table->n_nodes))
		return -1;

	hash = (((size_t)x << 3) ^ y) % table->n;

	/* Find and put */
	for (node = table->table[hash]; node; node = node->next) {
		if (node->xy == xy) {
			node->value = value;
			return 0;
		}
	}

	/* add */
	node = &table->nodes[table->idx++];
	node->value = value;
	node->next = table->table[hash];
	node->xy = xy;
	table->table[hash] = node;
	return 0;
}

/*
 *  qhash_get_node()
 *	find the hash table node of a (x,y) value in a hash table
 */
static sparse_qhash_node_t OPTIMIZE3 *qhash_get_node(void *handle, const uint32_t x, const uint32_t y)
{
	const sparse_qhash_table_t *table = (sparse_qhash_table_t *)handle;
	sparse_qhash_node_t *node;
	size_t hash;
	const uint64_t xy = ((uint64_t)x << 32) | y;

	if (UNLIKELY(!table))
		return NULL;
	hash = (((size_t)x << 3) ^ y) % table->n;

	for (node = table->table[hash]; node; node = node->next) {
		if (node->xy == xy)
			return node;
	}
	return NULL;
}

/*
 *  qhash_get()
 *	get the (x,y) value in hash table based sparse matrix
 */
static uint32_t OPTIMIZE3 qhash_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_qhash_node_t *node = qhash_get_node(handle, x, y);

	return node ? node->value : 0;
}

/*
 *  hash_del()
 *	zero the (x,y) value in sparse hash table
 */
static void qhash_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_qhash_node_t *node = qhash_get_node(handle, x, y);

	if (LIKELY(node != NULL))
		node->value = 0;
}

#if defined(HAVE_JUDY)

/*
 *  judy_create()
 *	create a judy array based sparse matrix
 */
static void *judy_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	static Pvoid_t PJLArray;

	(void)n;
	(void)x;
	(void)y;

	PJLArray = (Pvoid_t)NULL;
	return (void *)&PJLArray;
}

/*
 *  judy_destroy()
 *	destroy a judy array based sparse matrix
 */
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static void judy_destroy(void *handle, size_t *objmem)
{
	Word_t ret;

	JLMU(ret, *(Pvoid_t *)handle);
	*objmem = (size_t)ret;

	JLFA(ret, *(Pvoid_t *)handle);
	(void)ret;
}
STRESS_PRAGMA_POP

/*
 *  judy_put()
 *	put a value into a judy based sparse matrix
 */
static int OPTIMIZE3 judy_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	Word_t *pvalue;
	const Word_t idx = ((Word_t)x << 32) | y;

	JLI(pvalue, *(Pvoid_t *)handle, idx);
	if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR)))
		return -1;

	*pvalue = value;
	return 0;
}

/*
 *  judy_get()
 *	get the (x,y) value in judy array based sparse matrix
 */
static uint32_t OPTIMIZE3 judy_get(void *handle, const uint32_t x, const uint32_t y)
{
	Word_t *pvalue, value;
	const Word_t idx = ((Word_t)x << 32) | y;

	JLG(pvalue, *(Pvoid_t *)handle, idx);
	value = pvalue ? *pvalue : 0;
	return value;
}

/*
 *  judy_del()
 *	zero the (x,y) value in sparse judy array
 */
static void judy_del(void *handle, const uint32_t x, const uint32_t y)
{
	Word_t *pvalue;

	JLG(pvalue, *(Pvoid_t *)handle, ((Word_t)x << 32) | y);
	if (LIKELY(pvalue != NULL))
		*pvalue = 0;
}
#else
UNEXPECTED
#endif

#if defined(HAVE_RB_TREE) &&	\
    defined(RB_ENTRY)

static size_t rb_objmem;

/*
 *  sparse_rb_node_cmp()
 *	rb tree comparison function
 */
static int sparse_rb_node_cmp(sparse_rb_t *n1, sparse_rb_t *n2)
{
	register const uint64_t n1xy = n1->xy;
	register const uint64_t n2xy = n2->xy;

	if (n1xy == n2xy)
		return 0;
	else if (n1xy > n2xy)
		return 1;
	else
		return -1;
}

static RB_HEAD(sparse_rb_tree, sparse_rb) rb_root;
RB_PROTOTYPE(sparse_rb_tree, sparse_rb, rb, sparse_rb_node_cmp);
RB_GENERATE(sparse_rb_tree, sparse_rb, rb, sparse_rb_node_cmp);

/*
 *  rb_create()
 *	create a red black tree based sparse matrix
 */
static void *rb_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	(void)n;
	(void)x;
	(void)y;

	rb_objmem = 0;
	RB_INIT(&rb_root);
	return &rb_root;
}

/*
 *  rb_destroy()
 *	destroy a red black tree based sparse matrix
 */
static void rb_destroy(void *handle, size_t *objmem)
{
	*objmem = rb_objmem;
	rb_objmem = 0;
	(void)handle;
}

/*
 *  rb_put()
 *	put a value into a red black tree sparse matrix
 */
static int OPTIMIZE3 rb_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_rb_t node, *found;

	node.xy = ((uint64_t)x << 32) | y;
	found = RB_FIND(sparse_rb_tree, handle, &node);
	if (LIKELY(!found)) {
		sparse_rb_t *new_node;

		new_node = (sparse_rb_t *)malloc(sizeof(*new_node));
		if (!new_node)
			return -1;
		new_node->value = value;
		new_node->xy = node.xy;
		if (RB_INSERT(sparse_rb_tree, handle, new_node) != NULL)
			free(new_node);
		rb_objmem += sizeof(sparse_rb_t);
	} else {
		found->value = value;
	}
	return 0;
}

/*
 *  rb_del()
 *	zero the (x,y) value in red black tree sparse matrix
 */
static void OPTIMIZE3 rb_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_rb_t node, *found;
	node.xy = ((uint64_t)x << 32) | y;

	found = RB_FIND(sparse_rb_tree, handle, &node);
	if (UNLIKELY(!found))
		return;

	RB_REMOVE(sparse_rb_tree, handle, found);
	free(found);
}

/*
 *  rb_get()
 *	get the (x,y) value in a red back tree sparse matrix
 */
static uint32_t OPTIMIZE3 rb_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_rb_t node, *found;

	(void)shim_memset(&node, 0xff, sizeof(node));
	node.xy = ((uint64_t)x << 32) | y;

	found = RB_FIND(sparse_rb_tree, handle, &node);
	return found ? found->value : 0;
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SPLAY_TREE) &&	\
    defined(SPLAY_ENTRY)

static size_t splay_objmem;

/*
 *  sparse_splay_node_cmp()
 *	splay tree comparison function
 */
static int sparse_splay_node_cmp(sparse_splay_t *n1, sparse_splay_t *n2)
{
	register uint64_t n1xy = n1->xy;
	register uint64_t n2xy = n2->xy;

	if (n1xy == n2xy)
		return 0;
	else if (n1xy > n2xy)
		return 1;
	else
		return -1;
}

static SPLAY_HEAD(sparse_splay_tree, sparse_splay) splay_root;
SPLAY_PROTOTYPE(sparse_splay_tree, sparse_splay, splay, sparse_splay_node_cmp);
SPLAY_GENERATE(sparse_splay_tree, sparse_splay, splay, sparse_splay_node_cmp);

/*
 *  splay_create()
 *	create a splay tree based sparse matrix
 */
static void *splay_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	sparse_splay_t *node = NULL;

	(void)n;
	(void)x;
	(void)y;

	splay_objmem = 0;
	SPLAY_INIT(&splay_root);

	/* The following just silence warnings about unused SPLAY funcs */
	node = SPLAY_MIN(sparse_splay_tree, &splay_root);
	/* Node is always NULL, so this is a no-op */
	if (node)
		(void)SPLAY_NEXT(sparse_splay_tree, &splay_root, node);

	return &splay_root;
}

/*
 *  splay_destroy()
 *	destroy a splay tree based sparse matrix
 */
static void splay_destroy(void *handle, size_t *objmem)
{
	*objmem = splay_objmem;
	splay_objmem = 0;
	(void)handle;
}

/*
 *  splay_put()
 *	put a value into a splay tree sparse matrix
 */
static int OPTIMIZE3 splay_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_splay_t node, *found = NULL;

	node.xy = ((uint64_t)x << 32) | y;
	found = SPLAY_FIND(sparse_splay_tree, handle, &node);
	if (LIKELY(!found)) {
		sparse_splay_t *new_node;

		new_node = (sparse_splay_t *)malloc(sizeof(*new_node));
		if (UNLIKELY(!new_node))
			return -1;
		new_node->value = value;
		new_node->xy = node.xy;
		if (SPLAY_INSERT(sparse_splay_tree, handle, new_node) != NULL)
			free(new_node);
		splay_objmem += sizeof(sparse_splay_t);
	} else {
		found->value = value;
	}
	return 0;
}

/*
 *  splay_del()
 *	zero the (x,y) value in splay tree sparse matrix
 */
static void OPTIMIZE3 splay_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_splay_t node, *found = NULL;
	node.xy = ((uint64_t)x << 32) | y;

	found = SPLAY_FIND(sparse_splay_tree, handle, &node);
	if (UNLIKELY(!found))
		return;

	SPLAY_REMOVE(sparse_splay_tree, handle, found);
	free(found);
}

/*
 *  splay_get()
 *	get the (x,y) value in a splay tree sparse matrix
 */
static uint32_t OPTIMIZE3 splay_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_splay_t node, *found = NULL;

	(void)shim_memset(&node, 0xff, sizeof(node));
	node.xy = ((uint64_t)x << 32) | y;

	found = SPLAY_FIND(sparse_splay_tree, handle, &node);
	return found ? found->value : 0;
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_QUEUE_CIRCLEQ) &&	\
    defined(CIRCLEQ_ENTRY)
/*
 *  list_create()
 *	create a circular list based sparse matrix
 */
static void *list_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	static sparse_y_list_t y_head;

	CIRCLEQ_INIT(&y_head);
	(void)n;
	(void)x;
	(void)y;

	return (void *)&y_head;
}

/*
 *  list_destroy()
 *	destroy a circular list based sparse matrix
 */
static void list_destroy(void *handle, size_t *objmem)
{
	sparse_y_list_t *y_head = (sparse_y_list_t *)handle;

	*objmem = 0;
	while (!CIRCLEQ_EMPTY(y_head)) {
		sparse_y_list_node_t *y_node = (sparse_y_list_node_t *)CIRCLEQ_FIRST(y_head);

		sparse_x_list_t *x_head = &y_node->x_head;

		while (!CIRCLEQ_EMPTY(x_head)) {
			sparse_x_list_node_t *x_node = CIRCLEQ_FIRST(x_head);

			CIRCLEQ_REMOVE(x_head, x_node, sparse_x_list);
			*objmem += sizeof(*x_node);
			free(x_node);
		}
		CIRCLEQ_REMOVE(y_head, y_node, sparse_y_list);
		*objmem += sizeof(*y_node);
		free(y_node);
	}
}

/*
 *  list_put()
 *	put a value into a circular list based sparse matrix
 */
static int OPTIMIZE3 list_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_y_list_t *y_head = (sparse_y_list_t *)handle;
	sparse_y_list_node_t *y_node = NULL, *new_y_node;

	sparse_x_list_t *x_head;
	sparse_x_list_node_t *x_node = NULL, *new_x_node;

	CIRCLEQ_FOREACH(y_node, y_head, sparse_y_list) {
		if (y_node->y == y) {
			x_head = &y_node->x_head;
			goto find_x;
		}
		if (y_node->y > y) {
			new_y_node = (sparse_y_list_node_t *)malloc(sizeof(*new_y_node));
			if (UNLIKELY(!new_y_node))
				return -1;
			new_y_node->y = y;
			CIRCLEQ_INIT(&new_y_node->x_head);
			CIRCLEQ_INSERT_BEFORE(y_head, y_node, new_y_node, sparse_y_list);
			x_head = &new_y_node->x_head;
			goto find_x;
		}
	}

	new_y_node = (sparse_y_list_node_t *)malloc(sizeof(*new_y_node));
	if (UNLIKELY(!new_y_node))
		return -1;
	new_y_node->y = y;
	CIRCLEQ_INIT(&new_y_node->x_head);
	CIRCLEQ_INSERT_TAIL(y_head, new_y_node, sparse_y_list);
	x_head = &new_y_node->x_head;

find_x:
	CIRCLEQ_FOREACH(x_node, x_head, sparse_x_list) {
		if (x_node->x == x) {
			x_node->value = value;
			return 0;
		}
		if (x_node->x > x) {
			new_x_node = (sparse_x_list_node_t *)malloc(sizeof(*new_x_node));
			if (UNLIKELY(!new_x_node))
				return -1;  /* Leaves new_y_node allocated */
			new_x_node->x = x;
			new_x_node->value = value;
			CIRCLEQ_INSERT_BEFORE(x_head, x_node, new_x_node, sparse_x_list);
			return 0;
		}
	}
	new_x_node = (sparse_x_list_node_t *)calloc(1, sizeof(*new_x_node));
	if (UNLIKELY(!new_x_node))
		return -1;  /* Leaves new_y_node allocated */
	new_x_node->x = x;
	new_x_node->value = value;
	CIRCLEQ_INSERT_TAIL(x_head, new_x_node, sparse_x_list);
	return 0;
}

/*
 *  list_get_node()
 *	find the circular list node of a (x,y) value in a circular
 *	list based sparse matrix
 */
static sparse_x_list_node_t OPTIMIZE3 *list_get_node(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_y_list_t *y_head = (sparse_y_list_t *)handle;
	sparse_y_list_node_t *y_node;

	CIRCLEQ_FOREACH(y_node, y_head, sparse_y_list) {
		if (y_node->y == y) {
			sparse_x_list_node_t *x_node;

			CIRCLEQ_FOREACH(x_node, &y_node->x_head, sparse_x_list) {
				if (x_node->x == x)
					return x_node;
			}
			break;
		}
	}
	return NULL;
}

/*
 *  list_del()
 *	zero the (x,y) value in a circular list based sparse matrix
 */
static void list_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_x_list_node_t *x_node = list_get_node(handle, x, y);

	if (LIKELY(x_node != NULL))
		x_node->value = 0;
}

/*
 *  list_get()
 *	get the (x,y) value in a circular list based sparse matrix
 */
static uint32_t OPTIMIZE3 list_get(void *handle, const uint32_t x, const uint32_t y)
{
	const sparse_x_list_node_t *x_node = list_get_node(handle, x, y);

	return x_node ? x_node->value : 0;
}
#else
UNEXPECTED
#endif

#if defined(HAVE_JUDY)

/*
 *  hashjudy_create()
 *	create a hash table of Judy arrays
 */
static void *hashjudy_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	sparse_hashjudy_table_t *table;

	(void)n;
	(void)y;

	table = (sparse_hashjudy_table_t *)calloc(1, sizeof(*table));
	if (UNLIKELY(!table))
		return NULL;

	table->n = x;
	table->hash_table = (Pvoid_t *)calloc((size_t)x, sizeof(*table->hash_table));
	if (UNLIKELY(!table->hash_table)) {
		free(table);
		return NULL;
	}
	return (void *)table;
}

/*
 *  hashjudy_destroy()
 *	destroy a hash table of Judy arrays
 */
static void hashjudy_destroy(void *handle, size_t *objmem)
{
	sparse_hashjudy_table_t *table = (sparse_hashjudy_table_t *)handle;
	uint64_t i;

	*objmem = sizeof(*table) +
		  (size_t)table->n * sizeof(*table->hash_table);

	for (i = 0; i < table->n; i++) {
		Word_t ret;

		JLMU(ret, table->hash_table[i]);
		*objmem += (size_t)ret;
	}

	free(table->hash_table);
	table->n = 0;
	table->hash_table = NULL;
	free(table);
}

/*
 *  hashjudy_put()
 *	put a value into a hash based judy array
 */
static int OPTIMIZE3 hashjudy_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_hashjudy_table_t *table = (sparse_hashjudy_table_t *)handle;
	Word_t *pvalue;

	if (UNLIKELY(!table))
		return -1;

	JLI(pvalue, table->hash_table[x], y);
	if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR)))
		return -1;
	*pvalue = (Word_t)value;
	return 0;
}

/*
 *  hashjudy_del()
 *	put a value into a hash based judy array
 */
static void OPTIMIZE3 hashjudy_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hashjudy_table_t *table = (sparse_hashjudy_table_t *)handle;
	Word_t *pvalue;

	if (UNLIKELY(!table))
		return;

	JLG(pvalue, table->hash_table[x], y);
	if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR)))
		return;
	*pvalue = 0;
}

/*
 *  hashjudy_get()
 *	get the (x,y) value in hash judy array based sparse matrix
 */
static uint32_t OPTIMIZE3 hashjudy_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hashjudy_table_t *table = (sparse_hashjudy_table_t *)handle;
	Word_t *pvalue, value;

	if (UNLIKELY(!table))
		return -1;

	JLG(pvalue, table->hash_table[x], y);
	if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR)))
		return 0;
	value = *pvalue;
	return (uint32_t)value;
}

#endif

static int stress_sparse_method_test(
	stress_args_t *args,
	const uint64_t sparsematrix_items,
	const uint32_t sparsematrix_size,
	const stress_sparsematrix_method_info_t *info,
	test_info_t *test_info)
{
	void *handle;
	uint64_t i;
	int rc = SPARSE_TEST_OK;
	size_t objmem = 0;
	double t1, t2;

	const uint32_t w = stress_mwc32();
	const uint32_t z = stress_mwc32();

	handle = info->create(sparsematrix_items, sparsematrix_size, sparsematrix_size);
	if (UNLIKELY(!handle)) {
		test_info->skip_no_mem = true;
		return SPARSE_TEST_ENOMEM;
	}

	stress_mwc_set_seed(w, z);

	t1 = stress_time_now();
	for (i = 0; LIKELY(stress_continue_flag() && (i < sparsematrix_items)); i++) {
		register const uint32_t x = stress_mwc32modn(sparsematrix_size);
		register const uint32_t y = stress_mwc32modn(sparsematrix_size);
		uint32_t gv, v = value_map(x, y);

		if (v == 0)
			v = ~(uint32_t)0;

		gv = info->get(handle, x, y);
		if (gv == 0) {
			if (UNLIKELY(info->put(handle, x, y, v) < 0)) {
				pr_fail("%s: %s failed to put into "
					"sparse matrix at position "
					"(%" PRIu32 ",%" PRIu32 ")\n",
					args->name, info->name, x, y);
				rc = SPARSE_TEST_FAILED;
				goto err;
			}
		}
	}
	t2 = stress_time_now();
	test_info->put_ops += i;
	test_info->put_duration += (t2 - t1);

	stress_mwc_set_seed(w, z);
	t1 = stress_time_now();
	for (i = 0; LIKELY(stress_continue_flag() && (i < sparsematrix_items)); i++) {
		register const uint32_t x = stress_mwc32modn(sparsematrix_size);
		register const uint32_t y = stress_mwc32modn(sparsematrix_size);
		uint32_t gv, v = value_map(x, y);

		if (v == 0)
			v = ~(uint32_t)0;

		gv = info->get(handle, x, y);
		if (UNLIKELY(gv != v)) {
			pr_fail("%s: %s mismatch (%" PRIu32 ",%" PRIu32
				") was %" PRIu32 ", got %" PRIu32 "\n",
				args->name, info->name, x, y, v, gv);
			rc = SPARSE_TEST_FAILED;
			goto err;
		}
	}
	t2 = stress_time_now();
	test_info->get_ops += i;
	test_info->get_duration += (t2 - t1);

	/* Random fetches, most probably all zero unset values */
	t1 = stress_time_now();
	for (i = 0; LIKELY(stress_continue_flag() && (i < sparsematrix_items)); i++) {
		const uint32_t x = stress_mwc32modn(sparsematrix_size);
		const uint32_t y = stress_mwc32modn(sparsematrix_size);

		(void)info->get(handle, x, y);
	}
	t2 = stress_time_now();
	test_info->get_ops += i;
	test_info->get_duration += (t2 - t1);

	stress_mwc_set_seed(w, z);
	for (i = 0; LIKELY(stress_continue_flag() && (i < sparsematrix_items)); i++) {
		const uint32_t x = stress_mwc32modn(sparsematrix_size);
		const uint32_t y = stress_mwc32modn(sparsematrix_size);
		uint32_t v = value_map(x, y);
		(void)v;

		info->del(handle, x, y);
	}
err:
	info->destroy(handle, &objmem);
	if (objmem > test_info->max_objmem)
		test_info->max_objmem = objmem;

	return rc;
}

static void *mmap_create(const uint64_t n, const uint32_t x, const uint32_t y)
{
	const size_t page_size = stress_get_page_size();
	static sparse_mmap_t m;
	size_t shmall, freemem, totalmem, freeswap, totalswap;
	uint64_t max_phys, total_free, max_size_t;

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

	(void)n;

	/*
	 *  We do 2 x random gets so that can touch as much
	 *  2 x n x pages. Make sure there is enough spare
	 *  physical pages to allow this w/o OOMing
	 */
	max_phys = n * page_size * 2;

	m.mmap_size = (uint64_t)x * (uint64_t)y * sizeof(uint32_t);
	m.mmap_size = (m.mmap_size + page_size - 1) & (uint64_t)~(page_size - 1);

	total_free = (uint64_t)freemem + (uint64_t)freeswap;
	if (max_phys > total_free)
		return NULL;

	max_size_t = (uint64_t)(~(size_t)0);
	if (m.mmap_size >= max_size_t)
		return NULL;

	m.x = x;
	m.y = y;

	m.mmap = stress_mmap_populate(NULL, m.mmap_size,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (UNLIKELY(m.mmap == MAP_FAILED))
		return NULL;
	stress_set_vma_anon_name(m.mmap, m.mmap_size, "sparse-data");
	return (void *)&m;
}

static void mmap_destroy(void *handle, size_t *objmem)
{
	sparse_mmap_t *m = (sparse_mmap_t *)handle;
	const size_t page_size = stress_get_page_size();
	unsigned char *vec;
	size_t pages;

	*objmem = 0;
	if (UNLIKELY(!m))
		return;
	if (UNLIKELY(m->mmap == MAP_FAILED || !m->mmap))
		return;
	if (UNLIKELY(!m->mmap_size))
		return;

	pages = m->mmap_size / page_size;
	vec = (unsigned char *)calloc(pages, 1);
	if (!vec) {
		*objmem = m->mmap_size;
	} else {
		if (shim_mincore(m->mmap, m->mmap_size, vec) == 0) {
			size_t i;
			size_t n = 0;

			for (i = 0; i < pages; i++)
				n += vec[i] ? page_size : 0;
			*objmem = n;
		} else {
			*objmem = m->mmap_size;
		}
		free(vec);
	}
	(void)munmap(m->mmap, m->mmap_size);
}

static int mmap_put(void *handle, const uint32_t x, const uint32_t y, const uint32_t value)
{
	sparse_mmap_t *m = (sparse_mmap_t *)handle;
	off_t offset;

	if (UNLIKELY(m->x <= x || m->y <= y))
		return -1;

	offset = (x + ((uint64_t)m->y * y));
	*((uint32_t *)(m->mmap) + offset) = value;

	return 0;
}

static void mmap_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_mmap_t *m = (sparse_mmap_t *)handle;
	uint64_t offset;

	if (UNLIKELY(m->x <= x || m->y <= y))
		return;

	offset = (x + ((uint64_t)m->y * y));
	*((uint32_t *)(m->mmap) + offset) = 0;
}

static uint32_t mmap_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_mmap_t *m = (sparse_mmap_t *)handle;
	uint64_t offset;

	if (UNLIKELY(m->x <= x || m->y <= y))
		return (uint32_t)-1;

	offset = (x + ((uint64_t)m->y * y));
	return *((uint32_t *)(m->mmap) + offset);
}

/*
 * Table of sparse matrix stress methods
 */
static const stress_sparsematrix_method_info_t sparsematrix_methods[] = {
	{ "all",	NULL, NULL, NULL, NULL, NULL },
	{ "hash",	hash_create, hash_destroy, hash_put, hash_del, hash_get },
#if defined(HAVE_JUDY)
	{ "hashjudy",	hashjudy_create, hashjudy_destroy, hashjudy_put, hashjudy_del, hashjudy_get },
	{ "judy",	judy_create, judy_destroy, judy_put, judy_del, judy_get },
#endif
#if defined(HAVE_SYS_QUEUE_CIRCLEQ) &&	\
    defined(CIRCLEQ_ENTRY)
	{ "list",	list_create, list_destroy, list_put, list_del, list_get },
#endif
	{ "mmap",	mmap_create, mmap_destroy, mmap_put, mmap_del, mmap_get },
	{ "qhash",	qhash_create, qhash_destroy, qhash_put, qhash_del, qhash_get },
#if defined(HAVE_RB_TREE) &&	\
    defined(RB_ENTRY)
	{ "rb",		rb_create, rb_destroy, rb_put, rb_del, rb_get },
#endif
#if defined(HAVE_SPLAY_TREE) &&	\
    defined(SPLAY_ENTRY)
	{ "splay",	splay_create, splay_destroy, splay_put, splay_del, splay_get },
#endif
};

static const char *sparsematrix_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(sparsematrix_methods)) ? sparsematrix_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_sparsematrix_items,  "sparsematrix-items",  TYPE_ID_UINT64, MIN_SPARSEMATRIX_ITEMS, MAX_SPARSEMATRIX_ITEMS, NULL },
	{ OPT_sparsematrix_method, "sparsematrix-method", TYPE_ID_SIZE_T_METHOD, 0, 1, sparsematrix_method },
	{ OPT_sparsematrix_size,   "sparsematrix-size",   TYPE_ID_UINT32, MIN_SPARSEMATRIX_SIZE, MAX_SPARSEMATRIX_SIZE, NULL },
	END_OPT,
};

static void stress_sparsematrix_create_failed(stress_args_t *args, const char *name)
{
	pr_inf("%s: failed to create sparse matrix with '%s' method, out of memory\n",
		args->name, name);
}

/*
 *  stress_sparsematrix()
 *	stress sparsematrix
 */
static int stress_sparsematrix(stress_args_t *args)
{
	uint32_t sparsematrix_size = DEFAULT_SPARSEMATRIX_SIZE;
	uint64_t sparsematrix_items = DEFAULT_SPARSEMATRIX_ITEMS;
	uint64_t capacity;
	double percent_full, count;
	double puts_mantissa, gets_mantissa;
	uint64_t puts_exponent, gets_exponent;
	int rc = EXIT_NO_RESOURCE;
	test_info_t test_info[SIZEOF_ARRAY(sparsematrix_methods)];
	size_t i, begin, end;
	size_t method = 0;	/* All methods */

	for (i = 0; i < SIZEOF_ARRAY(test_info); i++) {
		test_info[i].skip_no_mem = false;
		test_info[i].max_objmem = 0;
		test_info[i].put_duration = 0.0;
		test_info[i].get_duration = 0.0;
		test_info[i].put_ops = 0;
		test_info[i].get_ops = 0;
	}

	(void)stress_get_setting("sparsematrix-method", &method);

	if (!stress_get_setting("sparsematrix-size", &sparsematrix_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sparsematrix_size = MAX_SPARSEMATRIX_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sparsematrix_size = MIN_SPARSEMATRIX_SIZE;
	}

	if (!stress_get_setting("sparsematrix-items", &sparsematrix_items)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sparsematrix_items = MAX_SPARSEMATRIX_ITEMS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sparsematrix_items= MIN_SPARSEMATRIX_ITEMS;
	}

	capacity = (uint64_t)sparsematrix_size * (uint64_t)sparsematrix_size;

	if (sparsematrix_items > capacity) {
		uint64_t new_items = capacity;

		if (stress_instance_zero(args)) {
			pr_inf("%s: %" PRIu64 " items in sparsematrix is too large, using %" PRIu64 " instead\n",
				args->name, sparsematrix_items, new_items);
		}
		sparsematrix_items = new_items;
	}
	percent_full = 100.0 * (double)sparsematrix_items / (double)capacity;
	if (stress_instance_zero(args)) {
		pr_inf("%s: %" PRIu64 " items in %" PRIu32 " x %" PRIu32 " sparse matrix (%.2f%% full)\n",
			args->name, sparsematrix_items,
			sparsematrix_size, sparsematrix_size,
			percent_full);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (method == 0) {	/* All methods */
			for (i = 1; i < SIZEOF_ARRAY(sparsematrix_methods); i++) {
				if (UNLIKELY(stress_sparse_method_test(args,
						(size_t)sparsematrix_items,
						sparsematrix_size,
						&sparsematrix_methods[i],
						&test_info[i]) == SPARSE_TEST_FAILED)) {
					stress_sparsematrix_create_failed(args, sparsematrix_methods[i].name);
					goto err;
				}
			}
		} else {
			if (UNLIKELY(stress_sparse_method_test(args,
					(size_t)sparsematrix_items,
					sparsematrix_size,
					&sparsematrix_methods[method],
					&test_info[method]) == SPARSE_TEST_FAILED)) {
				stress_sparsematrix_create_failed(args, sparsematrix_methods[method].name);
				goto err;
			}
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (method == 0) {	/* All methods */
		begin = 1;
		end = SIZEOF_ARRAY(sparsematrix_methods);
	} else {
		begin = method;
		end = method + 1;
	}
	if (end > SIZEOF_ARRAY(sparsematrix_methods))
		end = SIZEOF_ARRAY(sparsematrix_methods);

	puts_mantissa = 1.0;
	puts_exponent = 0;
	gets_mantissa = 1.0;
	gets_exponent = 0;
	count = 0.0;

	for (i = begin; (i < end); i++) {
		if (!test_info[i].skip_no_mem) {
			char tmp[32];
			double rate, f;
			int e;

			(void)snprintf(tmp, sizeof(tmp), "%s gets per sec", sparsematrix_methods[i].name);
			rate = test_info[i].get_duration > 0.0 ? (double)test_info[i].get_ops / test_info[i].get_duration : 0.0;
			stress_metrics_set(args, (i * 2) + 0, tmp,
				rate, STRESS_METRIC_HARMONIC_MEAN);

			f = frexp((double)rate, &e);
			puts_mantissa *= f;
			puts_exponent += e;

			(void)snprintf(tmp, sizeof(tmp), "%s puts per sec", sparsematrix_methods[i].name);
			rate = test_info[i].put_duration > 0.0 ? (double)test_info[i].put_ops / test_info[i].put_duration : 0.0;
			stress_metrics_set(args, (i * 2) + 1, tmp,
				rate, STRESS_METRIC_HARMONIC_MEAN);

			f = frexp((double)rate, &e);
			gets_mantissa *= f;
			gets_exponent += e;

			count += 1.0;
		}
	}
	if (count > 0.0) {
		double geomean, inverse_n = 1.0 / count;

		geomean = pow(puts_mantissa, inverse_n) *
			  pow(2.0, (double)puts_exponent * inverse_n);
		pr_dbg("%s: %.2f put ops per second (geometric mean of per stressor put rates)\n",
			args->name, geomean);
		geomean = pow(gets_mantissa, inverse_n) *
			  pow(2.0, (double)gets_exponent * inverse_n);
		pr_dbg("%s: %.2f get ops per second (geometric mean of per stressor put rates)\n",
			args->name, geomean);
	}

	rc = EXIT_SUCCESS;
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_sparsematrix_info = {
	.stressor = stress_sparsematrix,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
