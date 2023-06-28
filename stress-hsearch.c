/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#if defined(HAVE_SEARCH_H)
#include <search.h>
#endif

#define MIN_HSEARCH_SIZE	(1 * KB)
#define MAX_HSEARCH_SIZE	(4 * MB)
#define DEFAULT_HSEARCH_SIZE	(8 * KB)

static const stress_help_t help[] = {
	{ NULL,	"hsearch N",	  "start N workers that exercise a hash table search" },
	{ NULL,	"hsearch-ops N",  "stop after N hash search bogo operations" },
	{ NULL,	"hsearch-size N", "number of integers to insert into hash table" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  stress_set_hsearch_size()
 *      set hsearch size from given option string
 */
static int stress_set_hsearch_size(const char *opt)
{
	uint64_t hsearch_size;

	hsearch_size = stress_get_uint64(opt);
	stress_check_range("hsearch-size", hsearch_size,
		MIN_HSEARCH_SIZE, MAX_HSEARCH_SIZE);
	return stress_set_setting("hsearch-size", TYPE_ID_UINT64, &hsearch_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_hsearch_size,	stress_set_hsearch_size },
	{ 0,			NULL }
};

#if defined(HAVE_HSEARCH)

/*
 *  stress_hsearch()
 *	stress hsearch
 */
static int stress_hsearch(const stress_args_t *args)
{
	uint64_t hsearch_size = DEFAULT_HSEARCH_SIZE;
	size_t i, max;
	int ret = EXIT_FAILURE;
	char **keys;

	if (!stress_get_setting("hsearch-size", &hsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			hsearch_size = MAX_HSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			hsearch_size = MIN_HSEARCH_SIZE;
	}

	max = (size_t)hsearch_size;

	/* Make hash table with 25% slack */
	if (!hcreate(max + (max / 4))) {
		pr_fail("%s: hcreate of size %zd failed\n", args->name, max + (max / 4));
		return EXIT_FAILURE;
	}

	keys = calloc(max, sizeof(*keys));
	if (!keys) {
		pr_err("%s: cannot allocate keys\n", args->name);
		goto free_hash;
	}

	/* Populate hash, make it 100% full for worst performance */
	for (i = 0; i < max; i++) {
		char buffer[32];
		ENTRY e;

		(void)snprintf(buffer, sizeof(buffer), "%zu", i);
		keys[i] = strdup(buffer);
		if (!keys[i]) {
			pr_err("%s: cannot allocate key\n", args->name);
			goto free_all;
		}

		e.key = keys[i];
		e.data = (void *)i;

		if (hsearch(e, ENTER) == NULL) {
			pr_err("%s: cannot allocate new hash item\n", args->name);
			goto free_all;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; stress_continue_flag() && (i < max); i++) {
			ENTRY e, *ep;

			e.key = keys[i];
			e.data = NULL;	/* Keep Coverity quiet */
			ep = hsearch(e, FIND);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (ep == NULL) {
					pr_fail("%s: cannot find key %s\n", args->name, keys[i]);
				} else {
					if (i != (size_t)ep->data) {
						pr_fail("%s: hash returned incorrect data %zd\n", args->name, i);
					}
				}
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;

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
	hdestroy();

	return ret;
}

stressor_info_t stress_hsearch_info = {
	.stressor = stress_hsearch,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

stressor_info_t stress_hsearch_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without libc hsearch() support"
};

#endif
