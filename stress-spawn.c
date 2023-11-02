// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_SPAWN_H)
#include <spawn.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"spawn N",	"start N workers spawning stress-ng using posix_spawn" },
	{ NULL,	"spawn-ops N",	"stop after N spawn bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SPAWN_H) &&	\
    defined(HAVE_POSIX_SPAWN)

/*
 *  stress_spawn_supported()
 *      check that we don't run this as root
 */
static int stress_spawn_supported(const char *name)
{
	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  executable as root.
	 */
	if (geteuid() == 0) {
		pr_inf_skip("%s stressor must not run as root, skipping the stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_spawn()
 *	stress by forking and spawn'ing
 */
static int stress_spawn(const stress_args_t *args)
{
	char *path;
	char exec_path[PATH_MAX];
	uint64_t spawn_fails = 0, spawn_calls = 0;
	static char *argv_new[] = { NULL, "--exec-exit", NULL };
	static char *env_new[] = { NULL };

	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  spawnable process as root.
	 */
	if (geteuid() == 0) {
		pr_inf("%s: running as root, won't run test.\n", args->name);
		return EXIT_FAILURE;
	}

	/*
	 *  Determine our own self as the executable, e.g. run stress-ng
	 */
	path = stress_get_proc_self_exe(exec_path, sizeof(exec_path));
	if (!path) {
		if (args->instance == 0)
			pr_inf_skip("%s: skipping stressor, can't determine stress-ng "
				"executable name\n", args->name);
		return EXIT_NOT_IMPLEMENTED;
	}
	argv_new[0] = path;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
		pid_t pid;

		spawn_calls++;
		ret = posix_spawn(&pid, path, NULL, NULL, argv_new, env_new);
		if (ret < 0) {
			pr_fail("%s: posix_spawn failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			spawn_fails++;
		} else {
			int status;
			/* Parent, wait for child */

			(void)shim_waitpid(pid, &status, 0);
			stress_bogo_inc(args);
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				spawn_fails++;
		}
	} while (stress_continue(args));

	if ((spawn_fails > 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail("%s: %" PRIu64 " spawns failed (%.2f%%)\n",
			args->name, spawn_fails,
			(double)spawn_fails * 100.0 / (double)(spawn_calls));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_spawn_info = {
	.stressor = stress_spawn,
	.supported = stress_spawn_supported,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_spawn_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without spawn.h or posix_spawn()"
};
#endif
