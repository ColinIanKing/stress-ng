/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"brk N",	"start N workers performing rapid brk calls" },
	{ NULL,	"brk-ops N",	"stop after N brk bogo operations" },
	{ NULL, "brk-mlock",	"attempt to mlock newly mapped brk pages" },
	{ NULL,	"brk-notouch",	"don't touch (page in) new data segment page" },
	{ NULL,	NULL,		NULL }
};

typedef struct {
	bool brk_mlock;
	bool brk_notouch;
} brk_context_t;

static int stress_set_brk_mlock(const char *opt)
{
	return stress_set_setting_true("brk-mlock", opt);
}

static int stress_set_brk_notouch(const char *opt)
{
	return stress_set_setting_true("brk-notouch", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_brk_mlock,	stress_set_brk_mlock },
	{ OPT_brk_notouch,	stress_set_brk_notouch },
	{ 0,			NULL }
};

static int stress_brk_supported(const char *name)
{
	void *ptr;

	/*
	 *  Some flavours of FreeBSD don't support sbrk()
	 *  so check for this
	 */
	ptr = shim_sbrk(0);
	if ((ptr == (void *)-1) && (errno == ENOSYS)) {
		pr_inf_skip("%s: stressor will be skipped, sbrk() is not "
			"implemented on this system\n", name);
		return -1;
	}

	/*
	 *  check for brk() not being implemented too
	 */
	if ((shim_brk(ptr) < 0) && (errno == ENOSYS)) {
		pr_inf_skip("%s: stressor will be skipped, brk() is not "
			"implemented on this system\n", name);
		return -1;
	}

	return 0;
}

static int stress_brk_child(const stress_args_t *args, void *context)
{
	uint8_t *start_ptr;
	int i = 0;
	const size_t page_size = args->page_size;
	const brk_context_t *brk_context = (brk_context_t *)context;

	start_ptr = shim_sbrk(0);
	if (start_ptr == (void *) -1) {
		pr_fail("%s: sbrk(0) failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

#if defined(MCL_FUTURE)
	if (brk_context->brk_mlock)
		(void)shim_mlockall(MCL_FUTURE);
#else
	UNEXPECTED
#endif

	do {
		uint8_t *ptr;

		/* Low memory avoidance, re-start */
		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size))
			VOID_RET(int, shim_brk(start_ptr));

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
				VOID_RET(int, shim_brk(start_ptr));
			} else {
				pr_fail("%s: sbrk(%d) failed: errno=%d (%s)\n",
					args->name, (int)page_size, errno,
					strerror(errno));
				return EXIT_FAILURE;
			}
		} else {
			/* Touch page, force it to be resident */
			if (!brk_context->brk_notouch)
				*(ptr - 1) = 0;

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_MERGEABLE)
			(void)madvise(ptr, page_size, MADV_MERGEABLE);
#else
			UNEXPECTED
#endif
		}
		inc_counter(args);
	} while (keep_stressing(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
static int stress_brk(const stress_args_t *args)
{
	int rc;
	brk_context_t brk_context;

	brk_context.brk_mlock = false;
	brk_context.brk_notouch = false;

	(void)stress_get_setting("brk-mlock", &brk_context.brk_mlock);
	(void)stress_get_setting("brk-notouch", &brk_context.brk_notouch);

#if !defined(MCL_FUTURE)
	if ((args->instance == 0) && brk_context.brk_mlock) {
		pr_inf("%s: --brk-mlock option was enabled but support for "
			"mlock(MCL_FUTURE) is not available\n",
			args->name);
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, (void *)&brk_context, stress_brk_child, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_brk_info = {
	.stressor = stress_brk,
	.supported = stress_brk_supported,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
