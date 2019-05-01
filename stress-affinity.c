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
	{ NULL,	"affinity N",	 "start N workers that rapidly change CPU affinity" },
	{ NULL,	"affinity-ops N","stop after N affinity bogo operations" },
	{ NULL,	"affinity-rand", "change affinity randomly rather than sequentially" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_affinity_rand(const char *opt)
{
	bool affinity_rand = true;

	(void)opt;
	return set_setting("affinity-rand", TYPE_ID_BOOL, &affinity_rand);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_affinity_rand,    stress_set_affinity_rand },
	{ 0,			NULL }
};

/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */

#if defined(HAVE_AFFINITY) && \
    defined(HAVE_SCHED_GETAFFINITY)

/*
 *  stress_affinity_supported()
 *      check that we can set affinity
 */
static int stress_affinity_supported(void)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);

	if (sched_getaffinity(0, sizeof(mask), &mask) < 0) {
		pr_inf("affinity stressor cannot get CPU affinity, skipping the stressor\n");
		return -1;
	}
	if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
		if (errno == EPERM) {
			pr_inf("affinity stressor cannot set CPU affinity, "
			       "process lacks privilege, skipping the stressor\n");
			return -1;
		}
	}
        return 0;
}

static int stress_affinity(const args_t *args)
{
	uint32_t cpu = args->instance;
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
	cpu_set_t mask;
	bool affinity_rand = false;

	(void)get_setting("affinity-rand", &affinity_rand);

	do {
		cpu = affinity_rand ?  (mwc32() >> 4) : cpu + 1;
		cpu %= cpus;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
			if (errno == EINVAL) {
				/*
				 * We get this if CPU is offline'd,
				 * and since that can be dynamically
				 * set, we should just retry
				 */
				continue;
			}
			pr_fail("%s: failed to move to CPU %" PRIu32
				", errno=%d (%s)\n",
				args->name, cpu, errno, strerror(errno));
			(void)shim_sched_yield();
		} else {
			/* Now get and check */
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
				if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (!CPU_ISSET(cpu, &mask)))
					pr_fail("%s: failed to move "
						"to CPU %" PRIu32 "\n",
						args->name, cpu);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_affinity_info = {
	.stressor = stress_affinity,
	.class = CLASS_SCHEDULER,
	.supported = stress_affinity_supported,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_affinity_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
