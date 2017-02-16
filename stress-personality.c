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
int stress_personality(const args_t *args)
{
	const ssize_t n = SIZEOF_ARRAY(personalities);
	bool failed[n];

	if (n == 0) {
		pr_inf("%s: no personalities to stress test\n", args->name);
		return EXIT_NOT_IMPLEMENTED;
	}
	memset(failed, 0, sizeof(failed));

	if (args->instance == 0)
		pr_dbg("%s: exercising %zu personalities\n", args->name, n);

	do {
		ssize_t i, fails = 0;

		for (i = 0; i < n; i++) {
			unsigned long p = personalities[i];
			int ret;

			if (!g_keep_stressing_flag)
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
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret & 0xff) != (p & 0xff)) {
				pr_fail("%s: fetched personality does "
					"not match set personality 0x%lu\n",
					args->name, p);
			}
		}
		if (fails == n) {
			pr_fail("%s: all %zu personalities failed "
				"to be set\n", args->name, fails);
			break;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_personality(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
