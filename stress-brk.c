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
	{ NULL,	"brk N",	"start N workers performing rapid brk calls" },
	{ NULL,	"brk-ops N",	"stop after N brk bogo operations" },
	{ NULL,	"brk-notouch",	"don't touch (page in) new data segment page" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_brk_notouch(const char *opt)
{
	(void)opt;
	bool brk_notouch = true;

	return stress_set_setting("brk-notouch", TYPE_ID_BOOL, &brk_notouch);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_brk_notouch,	stress_set_brk_notouch },
	{ 0,			NULL }
};

static int stress_brk_child(const stress_args_t *args, void *context)
{
	uint8_t *start_ptr;
	int i = 0;
	const size_t page_size = args->page_size;
	bool brk_notouch = false;

	(void)context;

	(void)stress_get_setting("brk-notouch", &brk_notouch);

	start_ptr = shim_sbrk(0);
	if (start_ptr == (void *) -1) {
		pr_fail_err("sbrk(0)");
		return EXIT_FAILURE;
	}

	do {
		uint8_t *ptr;

		i++;
		if (i < 8) {
			/* Expand brk by 1 page */
			ptr = shim_sbrk((intptr_t)page_size);
		} else if (i < 9) {
			/* brk to same brk position */
			ptr = shim_sbrk(0);
			if (shim_brk(ptr) < 0)
				ptr = (void *)-1;
		} else {
			/* Shrink brk by 1 page */
			i = 0;
			ptr = shim_sbrk(0);
			ptr -= page_size;
			if (shim_brk(ptr) < 0)
				ptr = (void *)-1;
		}

		if (ptr == (void *)-1) {
			if ((errno == ENOMEM) || (errno == EAGAIN)) {
				int ignore;

				ignore = shim_brk(start_ptr);
				(void)ignore;
			} else {
				pr_err("%s: sbrk(%d) failed: errno=%d (%s)\n",
					args->name, (int)page_size, errno,
					strerror(errno));
				return EXIT_FAILURE;
			}
		} else {
			/* Touch page, force it to be resident */
			if (!brk_notouch)
				*(ptr - 1) = 0;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
static int stress_brk(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_brk_child, STRESS_OOMABLE_DROP_CAP);
}

stressor_info_t stress_brk_info = {
	.stressor = stress_brk,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
