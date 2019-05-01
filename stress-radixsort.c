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
	{ NULL,	"radixsort N",	    "start N workers radix sorting random strings" },
	{ NULL,	"radixsort-ops N",  "stop after N radixsort bogo operations" },
	{ NULL,	"radixsort-size N", "number of strings to sort" },
	{ NULL,	NULL,		    NULL }
};

#if HAVE_LIB_BSD

#define STR_SIZE	(8)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

/*
 *  stress_radixsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_radixsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}
#endif

/*
 *  stress_set_radixsort_size()
 *	set radixsort size
 */
static int stress_set_radixsort_size(const char *opt)
{
	uint64_t radixsort_size;

	radixsort_size = get_uint64(opt);
	check_range("radixsort-size", radixsort_size,
		MIN_QSORT_SIZE, MAX_QSORT_SIZE);
	return set_setting("radixsort-size", TYPE_ID_UINT64, &radixsort_size);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_radixsort_size,	stress_set_radixsort_size },
	{ 0,			NULL }
};

#if HAVE_LIB_BSD
/*
 *  stress_radixsort()
 *	stress radixsort
 */
static int stress_radixsort(const args_t *args)
{
	uint64_t radixsort_size = DEFAULT_RADIXSORT_SIZE;
	const unsigned char **data;
	unsigned char *text, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;
	unsigned char revtable[256];

	if (!get_setting("radixsort-size", &radixsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			radixsort_size = MAX_RADIXSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			radixsort_size = MIN_RADIXSORT_SIZE;
	}
	n = (size_t)radixsort_size;

	text = calloc(n, STR_SIZE);
	if (!text) {
		pr_fail_dbg("calloc");
		return EXIT_NO_RESOURCE;
	}
	data = calloc(n, sizeof(*data));
	if (!data) {
		pr_fail_dbg("calloc");
		free(text);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_radixsort_handler, &old_action) < 0) {
		free(data);
		free(text);
		return EXIT_FAILURE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	for (i = 0; i < 256; i++)
		revtable[i] = 255 - i;

	/* This is very expensive, do it once */
	for (ptr = text, i = 0; i < n; i++, ptr += STR_SIZE) {
		data[i] = ptr;
		stress_strnrnd((char *)ptr, STR_SIZE);
	}

	do {
		/* Sort "random" data */
		(void)radixsort(data, n, NULL, 0);
		if (!g_keep_stressing_flag)
			break;

		/* Reverse sort */
		(void)radixsort(data, n, revtable, 0);

		/* Randomize first char */
		for (ptr = text, i = 0; i < n; i++, ptr += STR_SIZE)
			*ptr = 'a' + (mwc8() % 26);

		inc_counter(args);
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	free(data);
	free(text);

	return EXIT_SUCCESS;
}

stressor_info_t stress_radixsort_info = {
	.stressor = stress_radixsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_radixsort_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
