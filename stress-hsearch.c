/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include <search.h>

static uint64_t opt_hsearch_size = DEFAULT_HSEARCH_SIZE;
static bool set_hsearch_size = false;

/*
 *  stress_set_hsearch_size()
 *      set hsearch size from given option string
 */
void stress_set_hsearch_size(const char *optarg)
{
	set_hsearch_size = true;
	opt_hsearch_size = get_uint64_byte(optarg);
	check_range("hsearch-size", opt_hsearch_size,
		MIN_TSEARCH_SIZE, MAX_TSEARCH_SIZE);
}

/*
 *  stress_hsearch()
 *	stress hsearch
 */
int stress_hsearch(const args_t *args)
{
	size_t i, max;
	int ret = EXIT_FAILURE;
	char **keys;

	if (!set_hsearch_size) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_hsearch_size = MAX_HSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_hsearch_size = MIN_HSEARCH_SIZE;
	}

	max = (size_t)opt_hsearch_size;

	/* Make hash table with 25% slack */
	if (!hcreate(max + (max / 4))) {
		pr_fail_err("hcreate");
		return EXIT_FAILURE;
	}

	if ((keys = calloc(max, sizeof(char *))) == NULL) {
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

	do {
		for (i = 0; g_keep_stressing_flag && i < max; i++) {
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
		inc_counter(args);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;

free_all:
	/*
	 * The sematics to hdestroy are rather varied from
	 * system to system.  OpenBSD will free the keys,
	 * where as NetBSD provides traditional functionaly
	 * that does not free them, plus hdestroy1 where
	 * one can provide a free'ing callback.  Linux
	 * currently does not destroy them.  It's a mess,
	 * so for now, don't free them and just let it
	 * leak, the exit() will clean up the heap for us
	 */
	/*
	for (i = 0; i < max; i++)
		free(keys[i]);
	*/
	free(keys);
free_hash:
	hdestroy();

	return ret;
}
