/*
 * Copyright (C) 2023-2025 Colin Ian King
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
#if defined(HAVE_MPFR_H)
#include <mpfr.h>
#endif

#define MIN_MPFR_PRECISION	(32)
#define MAX_MPFR_PRECISION	(1000000)
#define DEFAULT_MPFR_PRECISION	(1000)

static const stress_help_t help[] = {
	{ NULL,	"mpfr N",		"start N workers performing multi-precision floating point operations" },
	{ NULL,	"mpfr-ops N",		"stop after N multi-precision floating point operations" },
	{ NULL,	"mpfr-precision N",	"specific floating point precision as N bits" },
	{ NULL,	NULL,		 	NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mpfr_precision, "mpfr-precision", TYPE_ID_UINT32, MIN_MPFR_PRECISION, MAX_MPFR_PRECISION, NULL },
	END_OPT,
};

#if defined(HAVE_GMP_H) &&	\
    defined(HAVE_MPFR_H) &&	\
    defined(HAVE_LIB_MPFR)

typedef void (*stress_mpfr_func_t)(const mpfr_prec_t precision, mpfr_t *result);

typedef struct {
	const char *name;
	const stress_mpfr_func_t mpfr_func;
} stress_mpfr_method_t;

/*
 *  stress_mpfr_euler()
 *  	compute e using: 1 + 1/1! +1/2! +... +1/100!
 */
static void stress_mpfr_euler(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t sum_prev, sum, t, u;
	int i, j;

	mpfr_init2(sum_prev, precision);
	mpfr_init2(sum, precision);
	mpfr_init2(t, precision);
	mpfr_init2(u, precision);

	for (j = 0; (j <= 10); j++) {
		mpfr_set_d(t, 1.0, MPFR_RNDD);
		mpfr_set_d(sum, 1.0, MPFR_RNDD);
		mpfr_set_d(sum_prev, 0.0, MPFR_RNDD);

		for (i = 1; i <= 1000; i++) {
			mpfr_set(sum_prev, sum, MPFR_RNDD);
			mpfr_mul_ui(t, t, i, MPFR_RNDU);
			mpfr_set_d(u, 1.0, MPFR_RNDD);
			mpfr_div(u, u, t, MPFR_RNDD);
			mpfr_add(sum, sum, u, MPFR_RNDD);
			if (mpfr_cmp(sum_prev, sum) == 0)
				break;
		}
		mpfr_set(*result, sum, MPFR_RNDD);
	}

	mpfr_clear(sum_prev);
	mpfr_clear(sum);
	mpfr_clear(t);
	mpfr_clear(u);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_omega()
 *  	See http://en.wikipedia.org/wiki/Omega_constant
 */
static void stress_mpfr_omega(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t omega, omega_prev, tmp1, tmp2;
	int i;

	mpfr_init2(omega, precision);
	mpfr_init2(omega_prev, precision);
	mpfr_init2(tmp1, precision);
	mpfr_init2(tmp2, precision);

	mpfr_set_d(omega, 0.5, MPFR_RNDD);

	for (i = 0; i <= 1000; i++) {
		mpfr_set(omega_prev, omega, MPFR_RNDD);

		mpfr_add_ui(tmp1, omega, 1UL, MPFR_RNDD);	/* tmp1 = 1 + omega */
		mpfr_exp(tmp2, omega, MPFR_RNDD);		/* tmp2 = exp(omega) */
		mpfr_add_ui(tmp2, tmp2, 1UL, MPFR_RNDD);	/* tmp2 = 1 + tmp2 */
		mpfr_div(omega, tmp1, tmp2, MPFR_RNDD);		/* omega = tmp1 / tmp 2 */

		if (mpfr_cmp(omega_prev, omega) == 0)
			break;
	}
	mpfr_set(*result, omega, MPFR_RNDD);

	mpfr_clear(omega_prev);
	mpfr_clear(omega);
	mpfr_clear(tmp1);
	mpfr_clear(tmp2);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_phi()
 *  	compute the Golden Ratio
 */
static void stress_mpfr_phi(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t phi, a, b, c;
	int i;

	mpfr_init2(phi, precision);
	mpfr_init2(a, precision);
	mpfr_init2(b, precision);
	mpfr_init2(c, precision);

	mpfr_set_ui(a, (unsigned long int)stress_mwc64(), MPFR_RNDD);
	mpfr_set_ui(b, (unsigned long int)stress_mwc64(), MPFR_RNDD);

	for (i = 0; i <= 1000; i++) {
		mpfr_add(c, a, b, MPFR_RNDD);
		mpfr_set(a, b, MPFR_RNDD);
		mpfr_set(b, c, MPFR_RNDD);
	}
	mpfr_div(phi, b, a, MPFR_RNDD);
	mpfr_set(*result, phi, MPFR_RNDD);

	mpfr_clear(phi);
	mpfr_clear(a);
	mpfr_clear(b);
	mpfr_clear(c);
	mpfr_free_cache();
}

static void stress_mpfr_nsqrt(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t val, lo, hi, tmp, sqroot;
	int i;

	mpfr_init2(val, precision);
	mpfr_init2(lo, precision);
	mpfr_init2(hi, precision);
	mpfr_init2(tmp, precision);
	mpfr_init2(sqroot, precision);

	mpfr_set_d(val, 65536.0, MPFR_RNDD);
	mpfr_set_d(lo, 1.0, MPFR_RNDD);
	mpfr_set(hi, val, MPFR_RNDD);

	for (i = 0; i <= 1000; i++) {
		int cmp;

		mpfr_add(sqroot, lo, hi, MPFR_RNDD);		/* g = lo + hi */
		mpfr_div_ui(sqroot, sqroot, 2, MPFR_RNDD);	/* g = g / 2 */
		mpfr_mul(tmp, sqroot, sqroot, MPFR_RNDD);	/* tmp = g ^ 2 */

		cmp = mpfr_cmp(tmp, val);
		if (cmp == 0)
			break;
		if (cmp > 0)					/* tmp > val? */
			mpfr_set(hi, sqroot, MPFR_RNDD);	/* hi = g */
		else
			mpfr_set(lo, sqroot, MPFR_RNDD);	/* lo = g */
	}
	mpfr_set(*result, sqroot, MPFR_RNDD);

	mpfr_clear(val);
	mpfr_clear(lo);
	mpfr_clear(hi);
	mpfr_clear(tmp);
	mpfr_clear(sqroot);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_apery()
 *  	compute Apéry's constant
 */
static void stress_mpfr_apery(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t apery, apery_prev, n3, tmp, zero;
	int i;

	mpfr_init2(apery, precision);
	mpfr_init2(apery_prev, precision);
	mpfr_init2(n3, precision);
	mpfr_init2(tmp, precision);
	mpfr_init2(zero, precision);

	mpfr_set_d(apery, 0.0, MPFR_RNDD);
	mpfr_set_d(zero, 0.0, MPFR_RNDD);

	for (i = 1; i <= 1000; i++) {
		mpfr_set(apery_prev, apery, MPFR_RNDD);

		mpfr_set_ui(tmp, (unsigned long int)i, MPFR_RNDD);
		mpfr_mul(n3, tmp, tmp, MPFR_RNDD);
		mpfr_mul(n3, n3, tmp, MPFR_RNDD);
		mpfr_ui_div(tmp, 1UL, n3, MPFR_RNDD);
		mpfr_add(apery, apery, tmp, MPFR_RNDD);

		mpfr_sub(tmp, apery, apery_prev, MPFR_RNDN);
		mpfr_prec_round(tmp, precision, MPFR_RNDN);
		if (mpfr_cmp(tmp, zero) == 0)
			break;
	}
	mpfr_set(*result, apery, MPFR_RNDD);

	mpfr_clear(apery_prev);
	mpfr_clear(apery);
	mpfr_clear(n3);
	mpfr_clear(tmp);
	mpfr_clear(zero);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_trigfunc()
 *  	compute trig function
 */
static void stress_mpfr_trigfunc(
	const mpfr_prec_t precision,
	mpfr_t *result,
	int (*trigfunc)(mpfr_t rop, const mpfr_t op, mpfr_rnd_t rnd))
{
	mpfr_t r, tmp, theta, dtheta;
	int i;

	mpfr_init2(r, precision);
	mpfr_init2(tmp, precision);
	mpfr_init2(theta, precision);
	mpfr_init2(dtheta, precision);

	mpfr_set_d(r, 0.0, MPFR_RNDD);
	mpfr_set_d(theta, 0.0, MPFR_RNDD);
	/* dtheta = pi / 100 */
	mpfr_const_pi(dtheta, MPFR_RNDD);
	mpfr_mul_ui(dtheta, dtheta, 2.0, MPFR_RNDD);
	mpfr_div_ui(dtheta, dtheta, 100UL, MPFR_RNDD);

	for (i = 1; i <= 100; i++) {
		trigfunc(tmp, theta, MPFR_RNDD);		/* tmp = trigfunc(theta); */
		mpfr_add(theta, theta, dtheta, MPFR_RNDD);
		mpfr_add(r, r, tmp, MPFR_RNDD);
	}
	mpfr_set(*result, r, MPFR_RNDD);

	mpfr_clear(r);
	mpfr_clear(tmp);
	mpfr_clear(theta);
	mpfr_clear(dtheta);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_cosine()
 *  	compute cosine
 */
static void stress_mpfr_cosine(const mpfr_prec_t precision, mpfr_t *result)
{
	stress_mpfr_trigfunc(precision, result, mpfr_cos);
}

/*
 *  stress_mpfr_sine()
 *  	compute sine
 */
static void stress_mpfr_sine(const mpfr_prec_t precision, mpfr_t *result)
{
	stress_mpfr_trigfunc(precision, result, mpfr_sin);
}

/*
 *  stress_mpfr_exp()
 *  	compute exponent
 */
static void stress_mpfr_exp(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t r, tmp;
	int i;

	mpfr_init2(r, precision);
	mpfr_init2(tmp, precision);

	mpfr_set_d(r, 0.0, MPFR_RNDD);

	for (i = 1; i <= 100; i++) {
		mpfr_set_ui(tmp, (unsigned long int)i, MPFR_RNDD);
		mpfr_exp(tmp, tmp, MPFR_RNDD);
		mpfr_add(r, r, tmp, MPFR_RNDD);
	}
	mpfr_set(*result, r, MPFR_RNDD);

	mpfr_clear(r);
	mpfr_clear(tmp);
	mpfr_free_cache();
}

/*
 *  stress_mpfr_ln()
 *  	compute natural log
 */
static void stress_mpfr_log(const mpfr_prec_t precision, mpfr_t *result)
{
	mpfr_t r, tmp;
	int i;

	mpfr_init2(r, precision);
	mpfr_init2(tmp, precision);

	mpfr_set_d(r, 0.0, MPFR_RNDD);

	for (i = 1; i <= 100; i++) {
		mpfr_set_ui(tmp, (unsigned long int)i, MPFR_RNDD);
		mpfr_log(tmp, tmp, MPFR_RNDD);
		mpfr_add(r, r, tmp, MPFR_RNDD);
	}
	mpfr_set(*result, r, MPFR_RNDD);

	mpfr_clear(r);
	mpfr_clear(tmp);
	mpfr_free_cache();
}

static const stress_mpfr_method_t stress_mpfr_methods[] = {
	{ "apery",	stress_mpfr_apery },
	{ "cosine",	stress_mpfr_cosine },
	{ "euler",	stress_mpfr_euler },
	{ "exp",	stress_mpfr_exp },
	{ "log",	stress_mpfr_log },
	{ "nsqrt",	stress_mpfr_nsqrt },
	{ "omega",	stress_mpfr_omega },
	{ "phi",	stress_mpfr_phi },
	{ "sine",	stress_mpfr_sine },
};

static int stress_mpfr(stress_args_t *args)
{
	mpfr_prec_t precision;
	uint32_t mpfr_precision = DEFAULT_MPFR_PRECISION;
	register size_t i;
	mpfr_t r0, r1;
	static stress_metrics_t metrics[SIZEOF_ARRAY(stress_mpfr_methods)];
	int rc = EXIT_SUCCESS;

	stress_zero_metrics(metrics, SIZEOF_ARRAY(metrics));

	if (!stress_get_setting("mpfr-precision", &mpfr_precision)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mpfr_precision = MAX_MPFR_PRECISION;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mpfr_precision = MIN_MPFR_PRECISION;
	}
	precision = (mpfr_prec_t)mpfr_precision;

	mpfr_init2(r0, precision);
	mpfr_init2(r1, precision);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint32_t w, z;

		stress_mwc_get_seed(&w, &z);

		for (i = 0; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(stress_mpfr_methods))); i++) {
			double t1;

			stress_mwc_set_seed(w, z);
			t1 = stress_time_now();
			stress_mpfr_methods[i].mpfr_func(precision, &r0);
			metrics[i].duration += stress_time_now() - t1;
			metrics[i].count += 1.0;
			stress_bogo_inc(args);

			stress_mwc_set_seed(w, z);
			t1 = stress_time_now();
			stress_mpfr_methods[i].mpfr_func(precision, &r1);
			metrics[i].duration += stress_time_now() - t1;
			metrics[i].count += 1.0;
			stress_bogo_inc(args);

			if (UNLIKELY(mpfr_cmp(r0, r1) != 0)) {
				pr_fail("%s: %s computation with %d precision inconsistent\n",
					args->name, stress_mpfr_methods[i].name, (int)precision);
				rc = EXIT_FAILURE;
				break;
			}
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	mpfr_clear(r0);
	mpfr_clear(r1);
	mpfr_free_cache();

	for (i = 0; i < SIZEOF_ARRAY(metrics); i++) {
		const double duration = metrics[i].duration;
		const double rate = duration > 0.0 ? metrics[i].count / duration : 0.0;
		char msg[80];

		(void)snprintf(msg, sizeof(msg), "%s %" PRIu32 " bit computations per sec",
				stress_mpfr_methods[i].name, mpfr_precision);
		stress_metrics_set(args, i, msg,
			rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	return rc;
}

const stressor_info_t stress_mpfr_info = {
	.stressor = stress_mpfr,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_mpfr_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without gmp.h, mpfr.h or libmpfr"
};

#endif
