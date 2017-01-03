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

#if defined(__linux__)

#include <sys/personality.h>

/* Personalities are determined at build time */
static const unsigned long personalities[] = {
#include "personality.h"
};

/*
 *  stress_personality()
 *	stress system by rapid open/close calls
 */
int stress_personality(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const ssize_t n = SIZEOF_ARRAY(personalities);
	bool failed[n];

	(void)instance;

	if (n == 0) {
		pr_inf(stderr, "%s: no personalities to stress test\n", name);
		return EXIT_NOT_IMPLEMENTED;
	}
	memset(failed, 0, sizeof(failed));

	if (instance == 0)
		pr_dbg(stderr, "%s: exercising %zu personalities\n", name, n);

	do {
		ssize_t i, fails = 0;

		for (i = 0; i < n; i++) {
			unsigned long p = personalities[i];
			int ret;

			if (!opt_do_run)
				break;
			if (failed[i]) {
				fails++;
				continue;
			}

			ret = personality(p);
			if (ret < 0) {
				failed[i] = true;
				continue;
			}
			ret = personality(~0UL);
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret & 0xff) != (p & 0xff)) {
				pr_fail(stderr, "%s: fetched personality does "
					"not match set personality 0x%lu\n",
					name, p);
			}
		}
		if (fails == n) {
			pr_fail(stderr, "%s: all %zu personalities failed "
				"to be set\n", name, fails);
			break;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#else
int stress_personality(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}
#endif
