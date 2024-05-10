/*
 * Copyright (C) 2024      Colin Ian King
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

#if defined(HAVE_GMP_H)
#include <gmp.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"factor N",		"start N workers performing large integer factorization" },
	{ NULL,	"factor-digits N",	"specific number of digits of number to factor" },
	{ NULL,	"factor-ops N",		"stop after N factorisation operations" },
	{ NULL,	NULL,		 	NULL }
};

static int stress_set_factor_digits(const char *opt)
{
	size_t factor_digits;

	factor_digits = (size_t)stress_get_int32(opt);
	stress_check_range("factor-digits", (uint64_t)factor_digits, 8, 100000000);
	return stress_set_setting("factor-digits", TYPE_ID_SIZE_T, &factor_digits);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_factor_digits,	stress_set_factor_digits },
	{ 0,			NULL },
};

#if defined(HAVE_GMP_H) &&	\
    defined(HAVE_LIB_GMP)

static int OPTIMIZE3 stress_factor(stress_args_t *args)
{
	size_t factor_digits = 10;
	double total_factors = 0.0, mean, t, duration = 0.0, rate;
	uint64_t ops, factors;
	mpz_t value, divisor, q, r, tmp;

	(void)stress_get_setting("factor-digits", &factor_digits);

	mpz_inits(value, divisor, q, r, tmp, NULL);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/* Step #1, generate a number to factorize */
		mpz_set_ui(value, 2);
		do {
			static const uint32_t base10[] = {
				1, 10, 100, 1000, 10000, 100000,
				1000000, 10000000, 100000000,
			};

			unsigned long int n;
			size_t digitsleft = factor_digits - mpz_sizeinbase(value, 10);

			if (!stress_continue_flag())
				goto abort;

			if (digitsleft > 6)
				digitsleft = 6;
			n = stress_mwc32modn(base10[digitsleft]) + 1;

			/* bump out small primes */
			if ((n & 1) == 0)
				n++;
			if ((n % 3) == 0)
				n += 2;

			mpz_set_ui(tmp, n);
			mpz_mul(value, value, tmp);
		} while (mpz_sizeinbase(value, 10) < factor_digits);

		/* Step #2, factorize it */
		t = stress_time_now();
		mpz_set_ui(divisor, 2);
		mpz_sqrt(tmp, value);
		factors = 0;

		while (mpz_cmp_ui(value, 1) != 0) {
			if (!stress_continue_flag())
				goto abort;
			mpz_cdiv_qr(q, r, value, divisor);

			if (mpz_cmp_ui(r, 0) == 0) {
				mpz_set(value, q);
				factors++;
			} else {
				mpz_nextprime(divisor, divisor);
			}
			if (mpz_cmp(divisor, tmp) > 0)
				break;
		}
		duration += stress_time_now() - t;
		total_factors += (double)factors;
		stress_bogo_inc(args);
	} while (stress_continue(args));

abort:

	mpz_clears(tmp, r, q, divisor, value, NULL);

	ops = stress_bogo_get(args);
	mean = (ops > 0) ? total_factors / (double)ops : 0.0;
	stress_metrics_set(args, 0, "average number of factors", mean, STRESS_METRIC_GEOMETRIC_MEAN);

	rate = (ops > 0) ? (double)duration / (double)ops : 0.0;
	stress_metrics_set(args, 1, "millisec per factorization", 1000.0 * rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_factor_info = {
	.stressor = stress_factor,
	.class = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};

#else

stressor_info_t stress_factor_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_COMPUTE,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without gmp.h, or libgmp"
};

#endif
