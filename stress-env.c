/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"env N",	"start N workers setting enironment vars" },
	{ NULL,	"env-ops N",	"stop after N env bogo operations" },
	{ NULL,	NULL,		NULL }
};

static int stress_env_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	uint64_t i = 0;
	size_t arg_max;
	const size_t arg_huge = 16 * MB;
	char *value;

	(void)context;

#if defined(_SC_ARG_MAX)
	arg_max = sysconf(_SC_ARG_MAX);
#else
	arg_max = 255;
#endif
#if defined(ARG_MAX)
	arg_max = STRESS_MAXIMUM(arg_max, ARG_MAX);
#elif defined(NCARGS)
	arg_max = STRESS_MAXIMUM(arg_max, NCARGS);
#endif
	if (arg_max > arg_huge)
		arg_max = arg_huge;
	if (arg_max < 1)
		arg_max = 1;

	/*
	 *  Try and allocate a large enough buffer
	 *  for the environment variable value
	 */
	for (;;) {
		value = (char *)mmap(NULL, arg_max, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (value != MAP_FAILED)
			break;
		if (arg_max <= page_size) {
			pr_inf("%s: could not allocate %zd bytes, "
				"errno = %d (%s)\n",
				args->name, arg_max,
				errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}

	stress_strnrnd(value, arg_max);

	do {
		char name[64];
		char tmp;
		int ret;
		const size_t sz = 1 + (stress_mwc32() % (arg_max - 2));

		snprintf(name, sizeof(name), "STRESS_ENV_%" PRIx64, i);
		/*
		 *  Set a random length for a variable
		 */
		tmp = value[sz];
		value[sz] = '\0';
		ret = setenv(name, value, 1);
		value[sz] = tmp;

		if (ret < 0) {
			if (errno == ENOMEM) {
				uint64_t j;

				for (j = 0; j < i; j++) {
					snprintf(name, sizeof(name), "STRESS_ENV_%" PRIx64, j);
					(void)unsetenv(name);
					inc_counter(args);
					if (!keep_stressing())
						goto reap;
				}
				i = 0;
			}
		} else {
			i++;
			inc_counter(args);
		}
	} while (keep_stressing());

reap:
	(void)munmap(value, arg_max);

	return EXIT_SUCCESS;
}

/*
 *  stress_env()
 *	stress environment variables
 */
static int stress_env(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_env_child, STRESS_OOMABLE_DROP_CAP);
}

stressor_info_t stress_env_info = {
	.stressor = stress_env,
	.class = CLASS_OS | CLASS_VM,
	.help = help
};
