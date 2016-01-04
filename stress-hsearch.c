/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <search.h>

#include "stress-ng.h"

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
int stress_hsearch(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	size_t i, max;
	int ret = EXIT_FAILURE;
	char **keys;

	(void)instance;

	if (!set_hsearch_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_hsearch_size = MAX_HSEARCH_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_hsearch_size = MIN_HSEARCH_SIZE;
	}

	max = (size_t)opt_hsearch_size;

	/* Make hash table with 25% slack */
	if (!hcreate(max + (max / 4))) {
		pr_fail_err(name, "hcreate");
		return EXIT_FAILURE;
	}

	if ((keys = calloc(max, sizeof(char *))) == NULL) {
		pr_err(stderr, "%s: cannot allocate keys\n", name);
		goto free_hash;
	}

	/* Populate hash, make it 100% full for worst performance */
	for (i = 0; i < max; i++) {
		char buffer[32];
		ENTRY e;

		snprintf(buffer, sizeof(buffer), "%zu", i);
		keys[i] = strdup(buffer);
		if (!keys[i]) {
			pr_err(stderr, "%s: cannot allocate key\n", name);
			goto free_all;
		}

		e.key = keys[i];
		e.data = (void *)i;

		if (hsearch(e, ENTER) == NULL) {
			pr_err(stderr, "%s: cannot allocate new hash item\n", name);
			goto free_all;
		}
	}

	do {
		for (i = 0; opt_do_run && i < max; i++) {
			ENTRY e, *ep;

			e.key = keys[i];
			e.data = NULL;	/* Keep Coverity quiet */
			ep = hsearch(e, FIND);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (ep == NULL) {
					pr_fail(stderr, "%s: cannot find key %s\n", name, keys[i]);
				} else {
					if (i != (size_t)ep->data) {
						pr_fail(stderr, "%s: hash returned incorrect data %zd\n", name, i);
					}
				}
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	ret = EXIT_SUCCESS;

free_all:
	for (i = 0; i < max; i++)
		free(keys[i]);
	free(keys);
free_hash:
	hdestroy();

	return ret;
}
