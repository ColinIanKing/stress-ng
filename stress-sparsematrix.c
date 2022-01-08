/*
 * Copyright (C)      2022 Colin Ian King.
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

/* Number of items in sparse matrix */
#define MIN_SPARSEMATRIX_ITEMS		(10)
#define MAX_SPARSEMATRIX_ITEMS		(10000000)
#define DEFAULT_SPARSEMATRIX_ITEMS	(5000)

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
    !defined(__APPLE__)
#define HAVE_RB_TREE	(1)
#endif

#if defined(HAVE_JUDY_H) && \
    defined(HAVE_LIB_JUDY)
#define HAVE_JUDY	(1)
#endif

typedef void * (*func_create)(const uint32_t n);
typedef int (*func_destroy)(void *handle);
typedef int (*func_put)(void *handle, const uint32_t x, const uint32_t y, const uint64_t value);
typedef int (*func_del)(void *handle, const uint32_t x, const uint32_t y);
typedef uint64_t (*func_get)(void *handle, const uint32_t x, const uint32_t y);

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
	{ NULL,	"sparsematrix-ops N",	 "stop after N bogo sparse matrix operations" },
	{ NULL,	"sparsematrix-method M", "select storage method: all, hash, judy, list or rb" },
	{ NULL,	"sparsematrix-items N",	 "N is the number of items in the spare matrix" },
	{ NULL,	"sparsematrix-size N",	 "M is the width and height X x Y of the matrix" },
	{ NULL,	NULL,		 NULL }
};

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

#if defined(HAVE_RB_TREE)
typedef struct sparse_rb {
	uint64_t xy;		/* x,y matrix position */
	uint64_t value;		/* value in matrix x,y */
	RB_ENTRY(sparse_rb) rb;	/* red-black tree node entry */
} sparse_rb_t;

#endif

typedef struct sparse_hash_node {
	uint64_t xy;		/* x,y matrix position */
	uint64_t value;		/* value in matrix x,y */
	struct sparse_hash_node *next;
} sparse_hash_node_t;

typedef struct sparse_hash_table {
	uint32_t n;		/* size of hash table */
	sparse_hash_node_t **table;
} sparse_hash_table_t;

#if defined(HAVE_SYS_QUEUE_CIRCLEQ)

typedef struct sparse_x_list_node {
	CIRCLEQ_ENTRY(sparse_x_list_node) sparse_x_list;
	uint32_t x;		/* x matrix position */
	uint64_t value;		/* value in matrix x,y */
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

#endif

/*
 *  stress_set_sparsematrix_items()
 *	set number of items to put into the sparse matrix
 */
static int stress_set_sparsematrix_items(const char *opt)
{
	uint64_t sparsematrix_items;

	sparsematrix_items = stress_get_uint64(opt);
	stress_check_range("sparsematrix-items", sparsematrix_items,
		MIN_SPARSEMATRIX_ITEMS, MAX_SPARSEMATRIX_ITEMS);
	return stress_set_setting("sparsematrix-items", TYPE_ID_UINT64, &sparsematrix_items);
}

/*
 *  stress_set_sparsematrix_size()
 *	set sparse matrix size (X x Y)
 */
static int stress_set_sparsematrix_size(const char *opt)
{
	uint32_t sparsematrix_size;

	sparsematrix_size = stress_get_uint32(opt);
	stress_check_range("sparsematrix-size", sparsematrix_size,
		MIN_SPARSEMATRIX_SIZE, MAX_SPARSEMATRIX_SIZE);
	return stress_set_setting("sparsematrix-size", TYPE_ID_UINT32, &sparsematrix_size);
}

/*
 *  stress_sparsematrix_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_sparsematrix_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  hash_create()
 *	create a hash table based sparse matrix
 */
static void *hash_create(const uint32_t n)
{
	sparse_hash_table_t *table;
	uint32_t n_prime = (size_t)stress_get_prime64((uint64_t)n);

	table = (sparse_hash_table_t *)calloc(1, sizeof(*table));
	if (!table)
		return NULL;

	table->table = (sparse_hash_node_t **)calloc((size_t)n_prime, sizeof(sparse_hash_node_t *));
	if (!table)
		return NULL;
	table->n = n_prime;

	return (void *)table;
}

/*
 *  hash_destroy()
 *	destroy a hash table based sparse matrix
 */
static int hash_destroy(void *handle)
{
	size_t i, n;
	sparse_hash_table_t *table = (sparse_hash_table_t *)handle;

	if (!handle)
		return -1;

	n = table->n;
	for (i = 0; i < n; i++) {
		sparse_hash_node_t *next;
		sparse_hash_node_t *node = table->table[i];

		while (node) {
			next = node->next;
			free(node);
			node = next;
		}
	}
	free(table->table);
	table->n = 0;
	table->table = 0;
	free(table);

	return 0;
}

/*
 *  hash_put()
 *	put a value into a hash based sparse matrix
 */
static int hash_put(void *handle, const uint32_t x, const uint32_t y, const uint64_t value)
{
	sparse_hash_node_t *node;
	sparse_hash_table_t *table = (sparse_hash_table_t *)handle;
	size_t hash;
	uint64_t xy = ((uint64_t)x << 32) | y;

	if (!table)
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

	node = calloc(1, sizeof(*node));
	if (!node)
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
static sparse_hash_node_t *hash_get_node(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hash_table_t *table = (sparse_hash_table_t *)handle;
	sparse_hash_node_t *node;
	size_t hash;
	uint64_t xy = ((uint64_t)x << 32) | y;

	if (!table)
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
static uint64_t hash_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hash_node_t *node = hash_get_node(handle, x, y);

	return node ? node->value : 0;
}

/*
 *  hash_del()
 *	zero the (x,y) value in sparse hash table
 */
static int hash_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_hash_node_t *node = hash_get_node(handle, x, y);

	if (node)
		node->value = 0;
	return 0;
}

#if defined(HAVE_JUDY)
/*
 *  judy_create()
 *	create a judy array based sparse matrix
 */
static void *judy_create(const uint32_t n)
{
	static Pvoid_t PJLArray;

	(void)n;
	PJLArray = (Pvoid_t)NULL;
	return (void *)&PJLArray;
}

/*
 *  judy_destroy()
 *	destroy a judy array based sparse matrix
 */
static int judy_destroy(void *handle)
{
	Word_t ret;

	JLFA(ret, *(Pvoid_t *)handle);
	(void)ret;

	return 0;
}

/*
 *  judy_put()
 *	put a value into a judy based sparse matrix
 */
static int judy_put(void *handle, const uint32_t x, const uint32_t y, const uint64_t value)
{
	Word_t *pvalue;

	JLI(pvalue, *(Pvoid_t *)handle, ((Word_t)x << 32) | y);
	if ((pvalue == NULL) || (pvalue == PJERR))
		return -1;

	*pvalue = (Word_t)value;
	return 0;
}

/*
 *  judy_get()
 *	get the (x,y) value in judy array based sparse matrix
 */
static uint64_t judy_get(void *handle, const uint32_t x, const uint32_t y)
{
	Word_t *pvalue;

	JLG(pvalue, *(Pvoid_t *)handle, ((Word_t)x << 32) | y);
	return pvalue ? *(uint64_t *)pvalue : 0;
}

/*
 *  judy_del()
 *	zero the (x,y) value in sparse judy array
 */
static int judy_del(void *handle, const uint32_t x, const uint32_t y)
{
	Word_t *pvalue;

	JLG(pvalue, *(Pvoid_t *)handle, ((Word_t)x << 32) | y);
	if (!pvalue)
		*pvalue = 0;
	return 0;
}
#endif

#if defined(HAVE_RB_TREE)

/*
 *  sparse_node_cmp()
 *	rb tree comparison function
 */
static int sparse_node_cmp(sparse_rb_t *n1, sparse_rb_t *n2)
{
	if (n1->xy == n2->xy)
		return 0;
	if (n1->xy > n2->xy)
		return 1;
	else
		return -1;
}

static RB_HEAD(sparse_rb_tree, sparse_rb) rb_root;
RB_PROTOTYPE(sparse_rb_tree, sparse_rb, rb, sparse_node_cmp);
RB_GENERATE(sparse_rb_tree, sparse_rb, rb, sparse_node_cmp);

/*
 *  rb_create()
 *	create a red black tree based sparse matrix
 */
static void *rb_create(const uint32_t n)
{
	(void)n;

	RB_INIT(&rb_root);
	return &rb_root;
}

/*
 *  rb_destroy()
 *	destroy a red black tree based sparse matrix
 */
static int rb_destroy(void *handle)
{
	(void)handle;

	return 0;
}

/*
 *  rb_put()
 *	put a value into a red black tree sparse matrix
 */
static int rb_put(void *handle, const uint32_t x, const uint32_t y, const uint64_t value)
{
	sparse_rb_t node, *found;

	node.xy = ((uint64_t)x << 32) | y;
	found = RB_FIND(sparse_rb_tree, handle, &node);
	if (!found) {
		sparse_rb_t *new_node;

		new_node = calloc(1, sizeof(*new_node));
		if (!new_node)
			return -1;
		new_node->value = value;
		new_node->xy = node.xy;
		RB_INSERT(sparse_rb_tree, handle, new_node);
	} else {
		found->value = value;
	}
	return 0;
}

/*
 *  rb_del()
 *	zero the (x,y) value in red black tree sparse matrix
 */
static int rb_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_rb_t node, *found;
	node.xy = ((uint64_t)x << 32) | y;

	found = RB_FIND(sparse_rb_tree, handle, &node);
	if (!found)
		return -1;

	RB_REMOVE(sparse_rb_tree, handle, found);
	free(found);
	return 0;
}

/*
 *  rb_get()
 *	get the (x,y) value in a red back tree sparse matrix
 */
static uint64_t rb_get(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_rb_t node, *found;

	memset(&node, 0xff, sizeof(node));
	node.xy = ((uint64_t)x << 32) | y;

	found = RB_FIND(sparse_rb_tree, handle, &node);
	return found ? found->value : 0;
}


#endif

#if defined(HAVE_SYS_QUEUE_CIRCLEQ)

/*
 *  list_create()
 *	create a circular list based sparse matrix
 */
static void *list_create(const uint32_t n)
{
	static sparse_y_list_t y_head;

	CIRCLEQ_INIT(&y_head);
	(void)n;

	return (void *)&y_head;
}

/*
 *  list_destroy()
 *	destroy a circular list based sparse matrix
 */
static int list_destroy(void *handle)
{
	sparse_y_list_t *y_head = (sparse_y_list_t *)handle;

	while (!CIRCLEQ_EMPTY(y_head)) {
		sparse_y_list_node_t *y_node = CIRCLEQ_FIRST(y_head);

		sparse_x_list_t *x_head = &y_node->x_head;

		while (!CIRCLEQ_EMPTY(x_head)) {
			sparse_x_list_node_t *x_node = CIRCLEQ_FIRST(x_head);

			CIRCLEQ_REMOVE(x_head, x_node, sparse_x_list);
			free(x_node);
		}
		CIRCLEQ_REMOVE(y_head, y_node, sparse_y_list);
		free(y_node);
	}
	return 0;
}

/*
 *  rb_put()
 *	put a value into a circular list based sparse matrix
 */
static int list_put(void *handle, const uint32_t x, const uint32_t y, const uint64_t value)
{
	sparse_y_list_t *y_head = (sparse_y_list_t *)handle;
	sparse_y_list_node_t *y_node, *new_y_node;

	sparse_x_list_t *x_head;
	sparse_x_list_node_t *x_node, *new_x_node;

	CIRCLEQ_FOREACH(y_node, y_head, sparse_y_list) {
		if (y_node->y == y) {
			x_head = &y_node->x_head;
			goto find_x;
		}
		if (y_node->y > y) {
			new_y_node = calloc(1, sizeof(*new_y_node));
			if (!new_y_node)
				return -1;
			new_y_node->y = y;
			CIRCLEQ_INIT(&new_y_node->x_head);
			CIRCLEQ_INSERT_BEFORE(y_head, y_node, new_y_node, sparse_y_list);
			x_head = &new_y_node->x_head;
			goto find_x;
		}
	}

	new_y_node = calloc(1, sizeof(*new_y_node));
	if (!new_y_node)
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
			new_x_node = calloc(1, sizeof(*new_x_node));
			if (!new_x_node)
				return -1;
			new_x_node->x = x;
			new_x_node->value = value;
			CIRCLEQ_INSERT_BEFORE(x_head, x_node, new_x_node, sparse_x_list);
			return 0;
		}
	}
	new_x_node = calloc(1, sizeof(*new_x_node));
	if (!new_x_node)
		return -1;
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
static sparse_x_list_node_t *list_get_node(void *handle, const uint32_t x, const uint32_t y)
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
static int list_del(void *handle, const uint32_t x, const uint32_t y)
{
	sparse_x_list_node_t *x_node = list_get_node(handle, x, y);

	if (x_node) {
		x_node->value = 0;
	}

	return 0;
}

/*
 *  list_get()
 *	get the (x,y) value in a circular list based sparse matrix
 */
static uint64_t list_get(void *handle, const uint32_t x, const uint32_t y)
{
	const sparse_x_list_node_t *x_node = list_get_node(handle, x, y);

	return x_node ? x_node->value : 0;
}
#endif

static uint64_t value_map(const uint32_t x, const uint32_t y)
{
	return ((uint64_t)~x << 11) ^ y;
}

static int stress_sparse_method_test(
	const stress_args_t *args,
	const uint64_t sparsematrix_items,
	const uint32_t sparsematrix_size,
	const stress_sparsematrix_method_info_t *info)
{
	void *handle;
	uint64_t i;

	const uint32_t w = stress_mwc32();
	const uint32_t z = stress_mwc32();

	handle = info->create(sparsematrix_size);
	if (!handle)
		return -1;

	stress_mwc_seed(w, z);
	for (i = 0; i < sparsematrix_items; i++) {
		const uint32_t x = stress_mwc32() % sparsematrix_size;
		const uint32_t y = stress_mwc32() % sparsematrix_size;
		uint64_t v = value_map(x, y);
		uint64_t gv;

		if (v == 0)
			v = ~0ULL;

		gv = info->get(handle, x, y);
		if (gv == 0)
			info->put(handle, x, y, v);
	}
	stress_mwc_seed(w, z);
	for (i = 0; i < sparsematrix_items; i++) {
		const uint32_t x = stress_mwc32() % sparsematrix_size;
		const uint32_t y = stress_mwc32() % sparsematrix_size;
		uint64_t v = value_map(x, y);
		uint64_t gv;

		if (v == 0)
			v = ~0ULL;

		gv = info->get(handle, x, y);
		if (gv != v) {
			pr_err("%s: mismatch (%" PRIu32 ",%" PRIu32
				") was %" PRIx64 ", got %" PRIx64 "\n",
				args->name, x, y, v, gv);
		}
	}

	/* Random fetches, most probably all zero unset values */
	for (i = 0; i < sparsematrix_items; i++) {
		const uint32_t x = stress_mwc32() % sparsematrix_size;
		const uint32_t y = stress_mwc32() % sparsematrix_size;

		(void)info->get(handle, x, y);
	}

	stress_mwc_seed(w, z);
	for (i = 0; i < sparsematrix_items; i++) {
		const uint32_t x = stress_mwc32() % sparsematrix_size;
		const uint32_t y = stress_mwc32() % sparsematrix_size;
		uint64_t v = value_map(x, y);
		(void)v;

		info->del(handle, x, y);
	}
	return info->destroy(handle);
}

/*
 * Table of sparse matrix stress methods
 */
static const stress_sparsematrix_method_info_t sparsematrix_methods[] = {
	{ "all",	NULL, NULL, NULL, NULL, NULL },
	{ "hash",	hash_create, hash_destroy, hash_put, hash_del, hash_get },
#if defined(HAVE_JUDY)
	{ "judy",	judy_create, judy_destroy, judy_put, judy_del, judy_get },
#endif
#if defined(HAVE_SYS_QUEUE_CIRCLEQ)
	{ "list",	list_create, list_destroy, list_put, list_del, list_get },
#endif
#if defined(HAVE_RB_TREE)
	{ "rb",		rb_create, rb_destroy, rb_put, rb_del, rb_get },
#endif
	{ NULL,		NULL, NULL, NULL, NULL, NULL },
};

/*
 *  stress_set_sparsematrix_method()
 *	set the default method
 */
static int stress_set_sparsematrix_method(const char *name)
{
	stress_sparsematrix_method_info_t const *info;

	for (info = sparsematrix_methods; info->name; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("sparsematrix-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "sparsematrix-method must be one of:");
	for (info = sparsematrix_methods; info->name; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sparsematrix_items,	stress_set_sparsematrix_items },
	{ OPT_sparsematrix_method,	stress_set_sparsematrix_method },
	{ OPT_sparsematrix_size,	stress_set_sparsematrix_size },
	{ 0,				NULL }
};

/*
 *  stress_sparsematrix()
 *	stress sparsematrix
 */
static int stress_sparsematrix(const stress_args_t *args)
{
	uint32_t sparsematrix_size = DEFAULT_SPARSEMATRIX_SIZE;
	uint64_t sparsematrix_items = DEFAULT_SPARSEMATRIX_ITEMS;
	uint64_t capacity;
	double percent_full;
	struct sigaction old_action;
	int ret;
	stress_sparsematrix_method_info_t const *all = &sparsematrix_methods[0];
	stress_sparsematrix_method_info_t const *info = all;

	(void)stress_get_setting("sparsematrix-method", &info);

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

	capacity = sparsematrix_size * sparsematrix_size;

	if (sparsematrix_items > capacity) {
		uint64_t new_items = capacity;

		if (args->instance == 0) {
			pr_inf("%s: %" PRIu64 " items in sparsematrix is too large, using %" PRIu64 " instead\n",
				args->name, sparsematrix_items, new_items);
		}
		sparsematrix_items = new_items;
	}
	percent_full = 100.0 * (double)sparsematrix_items / (double)capacity;
	if (args->instance == 0) {
		pr_inf("%s: %" PRIu64 " items in %" PRIu32 " x %" PRIu32 " sparse matrix (%.2f%% full)\n",
			args->name, sparsematrix_items,
			sparsematrix_size, sparsematrix_size,
			percent_full);
	}

	if (stress_sighandler(args->name, SIGALRM, stress_sparsematrix_handler, &old_action) < 0)
		return EXIT_FAILURE;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (info == all) {
			size_t i;

			for (i = 1; sparsematrix_methods[i].name; i++) {
				stress_sparse_method_test(args, (size_t)sparsematrix_items,
					(size_t)sparsematrix_size, &sparsematrix_methods[i]);
			}
		} else {
			stress_sparse_method_test(args, (size_t)sparsematrix_items,
				(size_t)sparsematrix_size, info);
		}

		inc_counter(args);
	} while (keep_stressing(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sparsematrix_info = {
	.stressor = stress_sparsematrix,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
