/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

typedef struct {
	double ts;		/* timestamp */
	uint32_t check0;	/* memory clobbering check canary */
	jmp_buf buf;		/* jmpbuf itself */
	uint32_t check1;	/* memory clobbering check canary */
} jmp_buf_check_t;

static jmp_buf_check_t bufchk;

static const stress_help_t help[] = {
	{ NULL,	"longjmp N",	 "start N workers exercising setjmp/longjmp" },
	{ NULL,	"longjmp-ops N", "stop after N longjmp bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static void OPTIMIZE1 NOINLINE NORETURN stress_longjmp_func(void)
{
	bufchk.ts = stress_time_now();
	longjmp(bufchk.buf, 1);	/* Jump out */

	_exit(EXIT_FAILURE);	/* Never get here */
}

/*
 *  stress_jmp()
 *	stress system by setjmp/longjmp calls
 */
static int OPTIMIZE1 stress_longjmp(const stress_args_t *args)
{
	int ret;
	static uint32_t check0, check1;
	static double t_total;
	static uint64_t n = 0;

	check0 = stress_mwc32();
	check1 = stress_mwc32();

	bufchk.check0 = check0;
	bufchk.check1 = check1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = setjmp(bufchk.buf);

	if (ret) {
		static int c = 0;

		t_total += (stress_time_now() - bufchk.ts);
		n++;
		/*
		 *  Sanity check to see if setjmp clobbers regions
		 *  before/after the jmpbuf
		 */
		if (bufchk.check0 != check0) {
			pr_err("%s: memory corrupted before jmpbuf region\n",
				args->name);
		}
		if (bufchk.check1 != check1) {
			pr_err("%s: memory corrupted before jmpbuf region\n",
				args->name);
		}

		if (c++ >= 1000) {
			inc_counter(args);
			c = 0;
		}
	}
	if (keep_stressing(args))
		stress_longjmp_func();

	if (n) {
		pr_dbg("%s: about %.3f nanoseconds per longjmp call\n",
			args->name, (double)STRESS_NANOSECOND * t_total / n);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_longjmp_info = {
	.stressor = stress_longjmp,
	.class = CLASS_CPU,
	.help = help
};
