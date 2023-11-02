// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
static int stress_personality(const stress_args_t *args)
{
	const size_t n = SIZEOF_ARRAY(personalities);
	bool *failed;

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
			break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(failed);

	return EXIT_SUCCESS;
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
