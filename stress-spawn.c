/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
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
static int stress_spawn_supported(void)
{
	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  executable as root.
	 */
        if (geteuid() == 0) {
		pr_inf("spawn stressor must not run as root, skipping the stressor\n");
                return -1;
        }
        return 0;
}

/*
 *  stress_spawn()
 *	stress by forking and spawn'ing
 */
static int stress_spawn(const args_t *args)
{
	char path[PATH_MAX + 1];
	ssize_t len;
	uint64_t spawn_fails = 0, spawn_calls = 0;
	static char *argv_new[] = { NULL, "--exec-exit", NULL };
	static char *env_new[] = { NULL };

	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  spawnable as root.
	 */
	if (geteuid() == 0) {
		pr_inf("%s: running as root, won't run test.\n", args->name);
		return EXIT_FAILURE;
	}

	/*
	 *  Determine our own self as the spawnutable, e.g. run stress-ng
	 */
	len = readlink("/proc/self/exe", path, sizeof(path));
	if (len < 0 || len > PATH_MAX) {
		if (errno == ENOENT) {
			pr_inf("%s: skipping stressor, can't determine stress-ng "
				"executable name\n", args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: readlink on /proc/self/exe failed\n", args->name);
		return EXIT_FAILURE;
	}
	path[len] = '\0';
	argv_new[0] = path;

	do {
		int ret;
		pid_t pid;

		spawn_calls++;
		ret = posix_spawn(&pid, path, NULL, NULL, argv_new, env_new);
		if (ret < 0) {
			pr_fail_err("posix_spawn()");
			spawn_fails++;
		} else {
			int status;
			/* Parent, wait for child */

			(void)shim_waitpid(pid, &status, 0);
			inc_counter(args);
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				spawn_fails++;
		}
	} while (keep_stressing());

	if ((spawn_fails > 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail("%s: %" PRIu64 " spawns failed (%.2f%%)\n",
			args->name, spawn_fails,
			(double)spawn_fails * 100.0 / (double)(spawn_calls));
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_spawn_info = {
	.stressor = stress_spawn,
	.supported = stress_spawn_supported,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_spawn_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
