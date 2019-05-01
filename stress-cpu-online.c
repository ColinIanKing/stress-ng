/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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
	{ NULL,	"cpu-online N",		"start N workers offlining/onlining the CPUs" },
	{ NULL,	"cpu-online-ops N",	"stop after N offline/online operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(__linux__)

/*
 *  stress_cpu_online_set()
 *	set a specified CPUs online or offline
 */
static int stress_cpu_online_set(
	const args_t *args,
	const int32_t cpu,
	const int setting)
{
	char filename[PATH_MAX];
	char data[3] = { '0' + setting, '\n', 0 };
	ssize_t ret;

	(void)snprintf(filename, sizeof(filename),
		"/sys/devices/system/cpu/cpu%" PRId32 "/online", cpu);

	ret = system_write(filename, data, sizeof data);
	if ((ret < 0) &&
	    ((ret != -EAGAIN) && (ret != -EINTR) &&
             (ret != -EBUSY) && (ret != -EOPNOTSUPP))) {
		pr_fail_err("write");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_online_supported()
 *      check if we can run this as root
 */
static int stress_cpu_online_supported(void)
{
	int ret;

        if (geteuid() != 0) {
                pr_inf("cpu-online stressor will be skipped, "
                        "need to be running as root for this stressor\n");
                return -1;
        }

	ret = system_write("/sys/devices/system/cpu/cpu1/online", "1\n", 2);
	if (ret < 0) {
                pr_inf("cpu-online stressor will be skipped, "
                        "cannot write to cpu1 online sysfs control file\n");
                return -1;
	}

        return 0;
}

/*
 *  stress_cpu_online
 *	stress twiddling CPUs online/offline
 */
static int stress_cpu_online(const args_t *args)
{
	const int32_t cpus = stress_get_processors_configured();
	int32_t i, cpu_online_count = 0;
	bool *cpu_online;
	int rc = EXIT_SUCCESS;

	if (geteuid() != 0) {
		if (args->instance == 0)
			pr_inf("%s: need root privilege to run "
				"this stressor\n", args->name);
		/* Not strictly a test failure */
		return EXIT_SUCCESS;
	}

	if ((cpus < 1) || (cpus > 65536)) {
		pr_inf("%s: too few or too many CPUs (found %" PRId32 ")\n",
			args->name, cpus);
		return EXIT_FAILURE;
	}

	cpu_online = calloc(cpus, sizeof(*cpu_online));
	if (!cpu_online) {
		pr_err("%s: out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/*
	 *  Determine how many CPUs we can online/offline via
	 *  the online sys interface
	 */
	for (i = 0; i < cpus; i++) {
		char filename[PATH_MAX];
		int ret;

		(void)snprintf(filename, sizeof(filename),
			"/sys/devices/system/cpu/cpu%" PRId32 "/online", i);
		ret = access(filename, O_RDWR);
		if (ret == 0) {
			cpu_online[i] = true;
			cpu_online_count++;
		}
	}
	if (cpu_online_count == 0) {
		pr_inf("%s: no CPUs can be set online/offline\n", args->name);
		free(cpu_online);
		return EXIT_FAILURE;
	}
	if ((args->num_instances > 1) &&
	    (g_opt_flags & OPT_FLAGS_CPU_ONLINE_ALL)) {
		if (args->instance == 0) {
			pr_inf("%s: disabling --cpu-online-all option because "
			       "more than 1 %s stressor is being invoked\n",
				args->name, args->name);
		}
		g_opt_flags &= ~OPT_FLAGS_CPU_ONLINE_ALL;
	}

	if ((args->instance == 0) &&
	    (g_opt_flags & OPT_FLAGS_CPU_ONLINE_ALL)) {
		pr_inf("%s: exercising all %" PRId32 " cpus\n",
			args->name, cpu_online_count + 1);
	}


	/*
	 *  Now randomly offline/online them all
	 */
	do {
		unsigned long cpu = mwc32() % cpus;

		/*
		 * Only allow CPU 0 to be offlined if OPT_FLAGS_CPU_ONLINE_ALL
		 * --cpu-online-all has been enabled
		 */
		if ((cpu == 0) && !(g_opt_flags & OPT_FLAGS_CPU_ONLINE_ALL))
			continue;
		if (cpu_online[cpu]) {
			rc = stress_cpu_online_set(args, cpu, 0);
			if (rc != EXIT_SUCCESS)
				break;
			rc = stress_cpu_online_set(args, cpu, 1);
			if (rc != EXIT_SUCCESS)
				break;
			inc_counter(args);
		}
	} while (keep_stressing());

	/*
	 *  Force CPUs all back online
	 */
	for (i = 0; i < cpus; i++) {
		if (cpu_online[i])
			(void)stress_cpu_online_set(args, i, 1);
	}
	free(cpu_online);

	return rc;
}

stressor_info_t stress_cpu_online_info = {
	.stressor = stress_cpu_online,
	.supported = stress_cpu_online_supported,
	.class = CLASS_CPU | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#else
stressor_info_t stress_cpu_online_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#endif
