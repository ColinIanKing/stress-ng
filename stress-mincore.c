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
	{ NULL,	"mincore N",	  "start N workers exercising mincore" },
	{ NULL,	"mincore-ops N",  "stop after N mincore bogo operations" },
	{ NULL,	"mincore-random", "randomly select pages rather than linear scan" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_mincore_rand(const char *opt)
{
	bool mincore_rand = true;

	(void)opt;
	return set_setting("mincore-rand", TYPE_ID_BOOL, &mincore_rand);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_mincore_rand,     stress_set_mincore_rand },
	{ 0,			NULL }
};

#if defined(HAVE_MINCORE) && NEED_GLIBC(2,2,0)

#define VEC_MAX_SIZE 	(64)

/*
 *  stress_mincore()
 *	stress mincore system call
 */
static int stress_mincore(const args_t *args)
{
	uint8_t *addr = 0, *prev_addr = 0;
	const size_t page_size = args->page_size;
	const ptrdiff_t mask = ~(page_size - 1);
	bool mincore_rand = false;

	(void)get_setting("mincore-rand", &mincore_rand);

	do {
		int i;

		for (i = 0; (i < 100) && g_keep_stressing_flag; i++) {
			int ret, redo = 0;
			unsigned char vec[1];

redo: 			errno = 0;
			ret = shim_mincore((void *)addr, page_size, vec);
			if (ret < 0) {
				switch (errno) {
				case ENOMEM:
					/* Page not mapped */
					break;
				case EAGAIN:
					if (++redo < 100)
						goto redo;
					/* fall through */
				case ENOSYS:
					pr_inf("%s: mincore no not implemented, skipping stressor\n",
						args->name);
					return EXIT_NOT_IMPLEMENTED;
				default:
					pr_fail("%s: mincore on address %p error: %d %s\n",
						args->name, addr, errno,
						strerror(errno));
					return EXIT_FAILURE;
				}
			}
			if (mincore_rand) {
				addr = (uint8_t *)(ptrdiff_t)
					(((ptrdiff_t)addr >> 1) & mask);
				if (addr == prev_addr)
					addr = (uint8_t *)((ptrdiff_t)(mwc64() & mask));
				prev_addr = addr;
			}
			else
				addr += page_size;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_mincore_info = {
	.stressor = stress_mincore,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_mincore_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
