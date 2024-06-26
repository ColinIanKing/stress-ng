/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

#if defined(HAVE_SYS_PERSONALITY_H)
#include <sys/personality.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"personality N",	"start N workers that change their personality" },
	{ NULL,	"personality-ops N",	"stop after N bogo personality calls" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_PERSONALITY)

/* Personalities are determined at build time */
static const unsigned long personalities[] ALIGN64 = {
#include "personality.h"
};

/*
 *  stress_personality()
 *	stress system by rapid open/close calls
 */
static int stress_personality(stress_args_t *args)
{
	const size_t n = SIZEOF_ARRAY(personalities);
	bool *failed;
	int rc = EXIT_SUCCESS;

	if (n == 0) {
		pr_inf_skip("%s: no personalities to stress test, skipping stressor\n", args->name);
		return EXIT_NOT_IMPLEMENTED;
	}

	failed = (bool *)calloc(n, sizeof(*failed));
	if (!failed) {
		pr_inf_skip("%s: cannot allocate %zu boolean flags, skipping stressor\n",
			args->name, n);
		return EXIT_NO_RESOURCE;
	}

	if (args->instance == 0)
		pr_dbg("%s: exercising %zu personalities\n", args->name, n);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	do {
		size_t i, fails = 0;

		for (i = 0; i < n; i++) {
			const unsigned long p = personalities[i];
			int ret;

			if (!stress_continue_flag())
				break;
			if (UNLIKELY(failed[i])) {
				fails++;
				continue;
			}
			ret = personality(p);
			if (UNLIKELY(ret < 0)) {
				failed[i] = true;
				continue;
			}
			ret = personality(0xffffffffUL);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: failed to get personality, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				break;
			}
			/*
			 *  Exercise invalid personalities
			 */
			VOID_RET(int, personality(0xbad00000 | stress_mwc32()));
			VOID_RET(int, personality(p));
		}
		if (fails == n) {
			pr_fail("%s: all %zu personalities failed "
				"to be set\n", args->name, fails);
			rc = EXIT_FAILURE;
			break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(failed);

	return rc;
}

stressor_info_t stress_personality_info = {
	.stressor = stress_personality,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_personality_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/personality.h or personality() system call"
};
#endif
