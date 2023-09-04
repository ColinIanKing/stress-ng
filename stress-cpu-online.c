/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-builtin.h"

static const stress_help_t help[] = {
	{ NULL,	"cpu-online N",		"start N workers offlining/onlining the CPUs" },
	{ NULL, "cpu-online-affinity",	"set CPU affinity to the CPU to be offlined" },
	{ NULL, "cpu-online-all",	"attempt to exercise all CPUs include CPU 0" },
	{ NULL,	"cpu-online-ops N",	"stop after N offline/online operations" },
	{ NULL,	NULL,			NULL }
};

#define STRESS_CPU_ONLINE_MAX_CPUS	(65536)

static int stress_set_cpu_online_affinity(const char *opt)
{
	return stress_set_setting_true("cpu-online-affinity", opt);
}

static int stress_set_cpu_online_all(const char *opt)
{
	return stress_set_setting_true("cpu-online-all", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_cpu_online_affinity,	stress_set_cpu_online_affinity },
	{ OPT_cpu_online_all,		stress_set_cpu_online_all },
	{ 0,				NULL },
};

#if defined(__linux__)

/*
 *  stress_cpu_online_set_affinity(const uint32_t cpu)
 *	try to set cpu affinity
 */
static inline void stress_cpu_online_set_affinity(const uint32_t cpu)
{
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	VOID_RET(int, sched_setaffinity(0, sizeof(mask), &mask));
#else
	(void)cpu;
#endif
}

/*
 *  stress_cpu_online_set()
 *	set a specified CPUs online or offline
 */
static int stress_cpu_online_set(
	const stress_args_t *args,
	const uint32_t cpu,
	const int setting)
{
	char filename[PATH_MAX];
	char data[3] = { '0' + (char)setting, '\n', 0 };
	ssize_t ret;

	(void)snprintf(filename, sizeof(filename),
		"/sys/devices/system/cpu/cpu%" PRIu32 "/online", cpu);

	ret = stress_system_write(filename, data, sizeof(data));
	if (ret < 0) {
		switch (ret) {
		case -EAGAIN:
		case -EINTR:
		case -EBUSY:
		case -EOPNOTSUPP:
			/* Not strictly a failure */
			return EXIT_NO_RESOURCE;
		default:
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* Anything else is a failure */
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_online_get()
 *	get a specified CPUs online or offline state
 */
static int stress_cpu_online_get(const uint32_t cpu, int *setting)
{
	char filename[PATH_MAX];
	char data[3];
	ssize_t ret;

	(void)snprintf(filename, sizeof(filename),
		"/sys/devices/system/cpu/cpu%" PRIu32 "/online", cpu);

	(void)shim_memset(data, 0, sizeof(data));
	ret = stress_system_read(filename, data, sizeof(data));
	if (ret < 1) {
		*setting = -1;
		return EXIT_FAILURE;
	}
	switch (data[0]) {
	case '0':
		*setting = 0;
		break;
	case '1':
		*setting = 1;
		break;
	default:
		*setting = -1;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}


/*
 *  stress_cpu_online_supported()
 *      check if we can run this as root
 */
static int stress_cpu_online_supported(const char *name)
{
	ssize_t ret;

	if (geteuid() != 0) {
		pr_inf_skip("%s stressor will be skipped, "
		       "need to be running as root for this stressor\n", name);
		return -1;
	}

	ret = stress_system_write("/sys/devices/system/cpu/cpu1/online", "1\n", 2);
	if (ret < 0) {
		pr_inf_skip("%s stressor will be skipped, "
		       "cannot write to cpu1 online sysfs control file\n", name);
		return -1;
	}

	return 0;
}

/*
 *  stress_cpu_online
 *	stress twiddling CPUs online/offline
 */
static int stress_cpu_online(const stress_args_t *args)
{
	int32_t cpus = stress_get_processors_configured();
	int32_t i, cpu_online_count = 0;
	bool *cpu_online;
	bool cpu_online_affinity = false;
	bool cpu_online_all = false;
	int rc = EXIT_SUCCESS;
	double offline_duration = 0.0, offline_count = 0.0;
	double online_duration  = 0.0, online_count = 0.0;
	double rate;

	(void)stress_get_setting("cpu-online-affinity", &cpu_online_affinity);
	(void)stress_get_setting("cpu-online-all", &cpu_online_all);

	if (geteuid() != 0) {
		if (args->instance == 0)
			pr_inf("%s: need root privilege to run "
				"this stressor\n", args->name);
		/* Not strictly a test failure */
		return EXIT_SUCCESS;
	}

	if (cpus < 1) {
		pr_fail("%s: too few CPUs (detected %" PRId32 ")\n",
			args->name, cpus);
		return EXIT_FAILURE;
	}
	if (cpus > STRESS_CPU_ONLINE_MAX_CPUS) {
		pr_inf("%s: more than %" PRId32 " CPUs detected, "
			"limiting to %d\n",
			args->name, cpus, STRESS_CPU_ONLINE_MAX_CPUS);
		cpus = STRESS_CPU_ONLINE_MAX_CPUS;
	}

	cpu_online = calloc((size_t)cpus, sizeof(*cpu_online));
	if (!cpu_online) {
		pr_inf_skip("%s: out of memory allocating %" PRId32 " boolean flags, "
			    "skipping stressor\n", args->name, cpus);
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
	if ((args->num_instances > 1) && cpu_online_all) {
		if (args->instance == 0) {
			pr_inf("%s: disabling --cpu-online-all option because "
			       "more than 1 %s stressor is being invoked\n",
				args->name, args->name);
		}
		cpu_online_all = false;
	}

	if ((args->instance == 0) && cpu_online_all) {
		pr_inf("%s: exercising all %" PRId32 " cpus\n",
			args->name, cpu_online_count + 1);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Now randomly offline/online them all
	 */
	do {
		const uint32_t cpu = stress_mwc32modn((uint32_t)cpus);

		/*
		 *  Only allow CPU 0 to be offlined if --cpu-online-all has been enabled
		 */
		if ((cpu == 0) && !cpu_online_all)
			continue;
		if (cpu_online[cpu]) {
			double t;
			int setting;

			if (cpu_online_affinity)
				stress_cpu_online_set_affinity(cpu);

			t = stress_time_now();
			rc = stress_cpu_online_set(args, cpu, 0);
			if (rc == EXIT_FAILURE)
				break;
			if (rc == EXIT_SUCCESS) {
				rc = stress_cpu_online_get(cpu, &setting);
				if ((rc == EXIT_SUCCESS) && (setting != 0)) {
					pr_inf("%s: set cpu %" PRIu32 " offline, expecting setting to be 0, got %d instead\n",
						args->name, cpu, setting);
				} else {
					offline_duration += stress_time_now() - t;
					offline_count += 1.0;
				}
			}

			t = stress_time_now();
			rc = stress_cpu_online_set(args, cpu, 1);
			if (rc == EXIT_FAILURE)
				break;
			if (rc == EXIT_SUCCESS) {
				rc = stress_cpu_online_get(cpu, &setting);
				if ((rc == EXIT_SUCCESS) && (setting != 1)) {
					pr_inf("%s: set cpu %" PRIu32 " offline, expecting setting to be 1, got %d instead\n",
						args->name, cpu, setting);
				} else {
					online_duration += stress_time_now() - t;
					online_count += 1.0;
				}
			}
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  Force CPUs all back online
	 */
	for (i = 0; i < cpus; i++) {
		if (cpu_online[i])
			(void)stress_cpu_online_set(args, (uint32_t)i, 1);
	}
	free(cpu_online);

	rate = (offline_count > 0.0) ? (double)offline_duration / offline_count : 0.0;
	stress_metrics_set(args, 0, "millisecs per offline action", rate * STRESS_DBL_MILLISECOND);
	rate = (online_count > 0.0) ? (double)online_duration / online_count : 0.0;
	stress_metrics_set(args, 1, "millisecs per online action", rate * STRESS_DBL_MILLISECOND);

	return rc;
}

stressor_info_t stress_cpu_online_info = {
	.stressor = stress_cpu_online,
	.supported = stress_cpu_online_supported,
	.class = CLASS_CPU | CLASS_OS | CLASS_PATHOLOGICAL,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_cpu_online_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_OS | CLASS_PATHOLOGICAL,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
