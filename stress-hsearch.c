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
#include "core-builtin.h"
#include "core-prime.h"

#if defined(HAVE_SEARCH_H) && 	\
     defined(HAVE_HSEARCH)
#include <search.h>
#else
typedef enum {
	FIND,
	ENTER
} ACTION;

typedef struct {
	char *key;
	void *data;
} ENTRY;
#endif

#define MIN_HSEARCH_SIZE	(1 * KB)
#define MAX_HSEARCH_SIZE	(64 * MB)
#define DEFAULT_HSEARCH_SIZE	(8 * KB)

typedef int (*hcreate_func_t)(size_t nel);
typedef ENTRY *(*hsearch_func_t)(ENTRY item, ACTION action);
typedef void (*hdestroy_func_t)(void);

typedef struct {
	const char *name;
	hcreate_func_t hcreate;
	hsearch_func_t hsearch;
	hdestroy_func_t hdestroy;
} stress_hsearch_method_t;

static const stress_help_t help[] = {
	{ NULL,	"hsearch N",	  "start N workers that exercise a hash table search" },
	{ NULL,	"hsearch-ops N",  "stop after N hash search bogo operations" },
	{ NULL,	"hsearch-size N", "number of integers to insert into hash table" },
	{ NULL,	NULL,		  NULL }
};

typedef struct {
	uint32_t hash;
	ENTRY entry;
} hash_table_t;

static hash_table_t *htable;
static size_t htable_size;

static int hcreate_nonlibc(size_t nel)
{
	if (nel < 3)
		nel = 3;
	for (nel |= 1; ; nel += 2) {
		if (stress_is_prime64((uint64_t)nel))
			break;
	}
	htable = (hash_table_t *)calloc(nel, sizeof(*htable));
	if (!htable) {
		htable_size = 0;
		errno = ENOMEM;
		return 0;
	}
	htable_size = nel;
	return 1;
}

static void hdestroy_nonlibc(void)
{
	free(htable);
	htable = NULL;
	htable_size = 0;
}

static ENTRY OPTIMIZE3 *hsearch_nonlibc(ENTRY entry, ACTION action)
{
	register uint32_t idx, idx_start;
	register char *ptr = entry.key;

	for (idx = 0; *ptr; ) {
		idx += *(ptr++);
		idx = shim_ror32n(idx, 5);
	}
	idx %= (uint32_t)htable_size;
	if (idx == 0)
		idx = 1;
	idx_start = idx;

	if (action == FIND) {
		do {
			if ((htable[idx].hash == idx) && (strcmp(htable[idx].entry.key, entry.key) == 0))
				return &htable[idx].entry;
			idx++;
			if (idx >= htable_size)
				idx = 1;
		} while (idx != idx_start);
		return NULL;
	}
	do {
		register uint32_t hash = htable[idx].hash;

		if (hash == 0) {
			htable[idx].hash = idx;
			htable[idx].entry = entry;
			return &htable[idx].entry;
		} else if ((hash == idx) && (strcmp(htable[idx].entry.key, entry.key) == 0)) {
			htable[idx].hash = idx;
			htable[idx].entry = entry;
			return &htable[idx].entry;
		}
		idx++;
		if (idx >= htable_size)
			idx = 1;
	} while (idx != idx_start);

	return NULL;
}

static const stress_hsearch_method_t stress_hsearch_methods[] = {
#if defined(HAVE_SEARCH_H) &&	\
    defined(HAVE_HSEARCH)
	{ "hsearch-libc",	hcreate,	 hsearch,	hdestroy },
#endif
	{ "hsearch-nonlibc",	hcreate_nonlibc, hsearch_nonlibc, hdestroy_nonlibc },
};

static const char *stress_hsearch_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_hsearch_methods)) ? stress_hsearch_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_hsearch_method, "hsearch-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_hsearch_method },
	{ OPT_hsearch_size,   "hsearch-size",   TYPE_ID_UINT64, MIN_HSEARCH_SIZE, MAX_HSEARCH_SIZE, NULL },
	END_OPT,
};

/*
 *  stress_hsearch()
 *	stress hsearch
 */
static int OPTIMIZE3 stress_hsearch(stress_args_t *args)
{
	uint64_t hsearch_size = DEFAULT_HSEARCH_SIZE;
	size_t i, max;
	int rc = EXIT_FAILURE;
	char **keys;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	hsearch_func_t hsearch_func;
	hcreate_func_t hcreate_func;
	hdestroy_func_t hdestroy_func;
	size_t hsearch_method = 0;

	(void)stress_get_setting("hsearch-method", &hsearch_method);
	hcreate_func = stress_hsearch_methods[hsearch_method].hcreate;
	hsearch_func = stress_hsearch_methods[hsearch_method].hsearch;
	hdestroy_func = stress_hsearch_methods[hsearch_method].hdestroy;
	if (!stress_get_setting("hsearch-size", &hsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			hsearch_size = MAX_HSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			hsearch_size = MIN_HSEARCH_SIZE;
	}

	max = (size_t)hsearch_size;

	/* Make hash table with 25% slack */
	if (!hcreate_func(max + (max / 4))) {
		pr_fail("%s: hcreate of size %zu failed\n", args->name, max + (max / 4));
		return EXIT_FAILURE;
	}

	keys = (char **)calloc(max, sizeof(*keys));
	if (!keys) {
		pr_err("%s: cannot allocate %zu keys%s\n",
			args->name, max, stress_get_memfree_str());
		goto free_hash;
	}

	/* Populate hash, make it 100% full for worst performance */
	for (i = 0; i < max; i++) {
		char buffer[32];
		ENTRY e;

		(void)snprintf(buffer, sizeof(buffer), "%zu", i);
		keys[i] = shim_strdup(buffer);
		if (!keys[i]) {
			pr_err("%s: cannot allocate %zu byte key%s\n",
				args->name, strlen(buffer),
				stress_get_memfree_str());
			goto free_all;
		}

		e.key = keys[i];
		e.data = (void *)i;

		if (hsearch_func(e, ENTER) == NULL) {
			pr_err("%s: cannot allocate new hash item%s\n",
				args->name, stress_get_memfree_str());
			goto free_all;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = EXIT_SUCCESS;
	do {
		for (i = 0; LIKELY(stress_continue_flag() && (i < max)); i++) {
			ENTRY e;
			const ENTRY *ep;

			e.key = keys[i];
			e.data = NULL;	/* Keep Coverity quiet */
			ep = hsearch_func(e, FIND);
			if (verify) {
				if (UNLIKELY(ep == NULL)) {
					pr_fail("%s: cannot find key %s\n", args->name, keys[i]);
					rc = EXIT_FAILURE;
				} else {
					if (UNLIKELY(i != (size_t)ep->data)) {
						pr_fail("%s: hash returned incorrect data %zd\n", args->name, i);
						rc = EXIT_FAILURE;
					}
				}
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

free_all:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 * The semantics to hdestroy are rather varied from
	 * system to system.  OpenBSD will free the keys,
	 * where as NetBSD provides traditional functionality
	 * that does not free them, plus hdestroy1 where
	 * one can provide a free'ing callback.  Linux
	 * currently does not destroy them.  It's a mess,
	 * so for now, don't free them and just let it
	 * leak, the exit() will clean up the heap for us
	 * See: https://bugs.dragonflybsd.org/issues/1398
	 */
#if defined(__linux__)
	for (i = 0; i < max; i++)
		free(keys[i]);
#endif
	free(keys);
free_hash:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	hdestroy_func();

	return rc;
}

const stressor_info_t stress_hsearch_info = {
	.stressor = stress_hsearch,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
