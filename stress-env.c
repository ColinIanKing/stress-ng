/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-killpid.h"
#include "core-out-of-memory.h"

#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"env N",	"start N workers setting environment vars" },
	{ NULL,	"env-ops N",	"stop after N env bogo operations" },
	{ NULL,	NULL,		NULL }
};

static size_t stress_env_size(const size_t arg_max)
{
	return (size_t)(1 + stress_mwc32modn((uint32_t)(arg_max - 2)));
}

static uint64_t stress_env_max(void)
{
	return stress_mwc1() ? ~(uint64_t)0 : (uint64_t)(stress_mwc16());
}

static int stress_env_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	uint64_t i = 0;
	uint64_t env_max;
	uint32_t seed_w, seed_z;
	size_t arg_max;
	const size_t arg_huge = 16 * MB;
	char *value;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	(void)context;

#if defined(_SC_ARG_MAX)
	arg_max = (size_t)sysconf(_SC_ARG_MAX);
#else
	arg_max = 255;
#endif
#if defined(ARG_MAX)
	arg_max = STRESS_MAXIMUM(arg_max, ARG_MAX);
#elif defined(NCARGS)
	arg_max = STRESS_MAXIMUM(arg_max, NCARGS);
#endif
	if (arg_max > arg_huge)	/* cppcheck-suppress knownConditionTrueFalse */
		arg_max = arg_huge;
	if (arg_max < 1)	/* cppcheck-suppress knownConditionTrueFalse */
		arg_max = 1;

	/*
	 *  Try and allocate a large enough buffer
	 *  for the environment variable value
	 */
	value = (char *)mmap(NULL, arg_max, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (value == MAP_FAILED) {
		arg_max = page_size;
		value = (char *)mmap(NULL, arg_max, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (value == MAP_FAILED) {
			pr_inf_skip("%s: could not allocate %zu bytes for environment variable value%s, "
				"errno=%d (%s), skipping stressor\n",
				args->name, arg_max, stress_get_memfree_str(),
				errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		pr_inf("%s: falling back to %zu byte sized environment variable value size\n",
			args->name, arg_max);
	}
	stress_set_vma_anon_name(value, arg_max, "env-variable-value");

	stress_mwc_reseed();
	stress_rndstr(value, arg_max);
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	env_max = stress_env_max();
	stress_mwc_get_seed(&seed_w, &seed_z);

	do {
		char name[64];
		char tmp;
		int ret;
		const size_t sz = stress_env_size(arg_max);

		(void)snprintf(name, sizeof(name), "STRESS_ENV_%" PRIx64, i);
		/*
		 *  Set a random length for a variable
		 */
		tmp = value[sz];
		value[sz] = '\0';
		ret = setenv(name, value, 1);
		value[sz] = tmp;

		/* Low memory avoidance, re-start */
		if (stress_low_memory(arg_max * 2)) {
			(void)stress_kill_pid(getpid());
			_exit(EXIT_SUCCESS);
		}

		if ((i > env_max) || (ret < 0)) {
			uint64_t j;

			stress_mwc_set_seed(seed_w, seed_z);

			for (j = 0; j < i; j++) {
				if (verify) {
					const size_t env_sz = stress_env_size(arg_max);
					const char *val;

					(void)snprintf(name, sizeof(name), "STRESS_ENV_%" PRIx64, j);
					val = getenv(name);
					if (!val) {
						pr_fail("%s: cannot fetch environment variable %s\n",
							args->name, name);
						rc = EXIT_FAILURE;
					} else {
						tmp = value[env_sz];
						value[env_sz] = '\0';
						if (strcmp(value, val)) {
							pr_fail("%s: environment variable %s contains incorrect data\n",
								args->name, name);
							rc = EXIT_FAILURE;
						}
						value[env_sz] = tmp;
					}
				}
				ret = unsetenv(name);
				if (ret < 0) {
					pr_fail("%s: unsentenv on variable %s failed, errno=%d (%s)\n",
						args->name, name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
				stress_bogo_inc(args);
				if (UNLIKELY(!stress_continue(args)))
					goto reap;
			}
			i = 0;
			env_max = stress_env_max();
			stress_mwc_get_seed(&seed_w, &seed_z);
		} else {
			i++;
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)value, arg_max);

	return rc;
}

/*
 *  stress_env()
 *	stress environment variables
 */
static int stress_env(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_env_child, STRESS_OOMABLE_DROP_CAP | STRESS_OOMABLE_QUIET);
}

const stressor_info_t stress_env_info = {
	.stressor = stress_env,
	.classifier = CLASS_OS | CLASS_VM,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
