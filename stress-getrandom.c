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
	{ NULL,	"getrandom N",	   "start N workers fetching random data via getrandom()" },
	{ NULL,	"getrandom-ops N", "stop after N getrandom bogo operations" },
	{ NULL, NULL,		   NULL }
};

#if defined(__OpenBSD__) || 	\
    defined(__APPLE__) || 	\
    (defined(__linux__) && defined(__NR_getrandom))

#if defined(__OpenBSD__) ||	\
    defined(__APPLE__)
#define RANDOM_BUFFER_SIZE	(256)
#else
#define RANDOM_BUFFER_SIZE	(8192)
#endif

/*
 *  stress_getrandom_supported()
 *      check if getrandom is supported
 */
static int stress_getrandom_supported(void)
{
	int ret;
	char buffer[RANDOM_BUFFER_SIZE];

	ret = shim_getrandom(buffer, sizeof(buffer), 0);
	if ((ret < 0) && (errno == ENOSYS)) {
		pr_inf("getrandom stressor will be skipped, getrandom() not supported\n");
		return -1;
	}
	return 0;
}

/*
 *  stress_getrandom
 *	stress reading random values using getrandom()
 */
static int stress_getrandom(const args_t *args)
{
	do {
		char buffer[RANDOM_BUFFER_SIZE];

		ssize_t ret;

		ret = shim_getrandom(buffer, sizeof(buffer), 0);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == ENOSYS) {
				/* Should not happen.. */
				pr_inf("%s: stressor will be skipped, "
					"getrandom() not supported\n",
					args->name);
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail_err("getrandom");
			return EXIT_FAILURE;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_getrandom_info = {
	.stressor = stress_getrandom,
	.supported = stress_getrandom_supported,
	.class = CLASS_OS | CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_getrandom_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_CPU,
	.help = help
};
#endif
