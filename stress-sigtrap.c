// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
double t, duration;

static void MLOCKED_TEXT stress_sigtrap_handler(int num)
{
	const double delta = stress_time_now() - t;
	(void)num;

	if (delta > 0.0)
		duration += delta;
	counter++;
}

/*
 *  stress_sigtrap
 *	stress by generating traps (x86 only)
 */
static int stress_sigtrap(const stress_args_t *args)
{
	uint64_t raised = 0;
	double rate = 0.0;

	counter = 0;
	duration = 0.0;

	if (stress_sighandler(args->name, SIGTRAP, stress_sigtrap_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_continue(args)) {
		switch (stress_mwc1()) {
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
		case 0:
			t = stress_time_now();
			__asm__ __volatile__("int $3");
			raised++;
			break;
#endif
		default:
			t = stress_time_now();
			if (shim_raise(SIGTRAP) < 0) {
				pr_fail("%s: failed to raise SIGTRAP, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			raised++;
			break;
		}
		stress_bogo_set(args, counter);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if ((raised > 0) && (counter == 0)) {
		pr_fail("%s: %" PRIu64 " SIGTRAP%s raised, no SIGTRAPs handled\n",
			args->name, raised, raised > 1 ? "s" : "");
		return EXIT_FAILURE;
	}
	rate = (counter > 0) ? duration / (double)counter : 0.0;
	stress_metrics_set(args, 0, "nanosecs to handle SIGTRAP", rate * STRESS_DBL_NANOSECOND);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigtrap_info = {
	.stressor = stress_sigtrap,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
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
	.verify = VERIFY_ALWAYS,
        .help = help,
	.unimplemented_reason = "built without SIGTRAP signal number defined"
};
#endif
