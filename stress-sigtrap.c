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
#include "core-arch.h"

static const stress_help_t help[] = {
	{ NULL,	"sigtrap N",	 "start N workers generating SIGTRAP signals" },
	{ NULL,	"sigtrap-ops N", "stop after N bogo SIGTRAP operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(SIGTRAP)

static uint64_t counter;
static double t, duration;

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
static int stress_sigtrap(stress_args_t *args)
{
	uint64_t raised ALIGN64 = 0;
	double rate = 0.0;

	counter = 0;
	duration = 0.0;

	if (stress_sighandler(args->name, SIGTRAP, stress_sigtrap_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
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
	stress_metrics_set(args, 0, "nanosecs to handle SIGTRAP",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_sigtrap_info = {
	.stressor = stress_sigtrap,
	.classifier = CLASS_SIGNAL | CLASS_OS,
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

const stressor_info_t stress_sigtrap_info = {
        .stressor = stress_unimplemented,
        .supported = stress_sigtrap_supported,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
        .help = help,
	.unimplemented_reason = "built without SIGTRAP signal number defined"
};
#endif
