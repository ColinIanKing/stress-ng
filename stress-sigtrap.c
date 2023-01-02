/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-arch.h"

static const stress_help_t help[] = {
	{ NULL,	"sigtrap N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigtrap-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

#if defined(SIGTRAP)

static uint64_t counter;

static void MLOCKED_TEXT stress_sigtrap_handler(int num)
{
	(void)num;

	counter++;
}

/*
 *  stress_sigtrap
 *	stress by generating traps (x86 only)
 */
static int stress_sigtrap(const stress_args_t *args)
{
	counter = 0;

	if (stress_sighandler(args->name, SIGTRAP, stress_sigtrap_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (keep_stressing(args)) {
		switch (stress_mwc1()) {
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
		case 0:
			__asm__ __volatile__("int $3");
			break;
#endif
		default:
			raise(SIGTRAP);
			break;
		}
		set_counter(args, counter);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigtrap_info = {
	.stressor = stress_sigtrap,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else

static int stress_sigtrap_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, system "
		"does not support the SIGTRAP signal\n", name);
	return -1;
}

stressor_info_t stress_sigtrap_info = {
        .stressor = stress_unimplemented,
        .supported = stress_sigtrap_supported,
	.class = CLASS_INTERRUPT | CLASS_OS,
        .help = help,
	.unimplemented_reason = "built without SIGTRAP signal number defined"
};
#endif
