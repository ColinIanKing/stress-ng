// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#include <gmp.h>
#include <mpfr.h>

int main(void)
{
	mpfr_t v, one, pi;
	const mpfr_prec_t precision = 1000;

	mpfr_init2(v, precision);
	mpfr_init2(one, precision);
	mpfr_init2(pi, precision);

	mpfr_const_pi(pi, MPFR_RNDD);

	mpfr_set_d(v, 1000.0, MPFR_RNDD);
	mpfr_set_ui(one, 1UL, MPFR_RNDD);

	mpfr_mul(v, v, one, MPFR_RNDD);
	mpfr_mul_ui(v, one, 10, MPFR_RNDU);
	mpfr_add_ui(v, v, 2, MPFR_RNDD);
	mpfr_div(v, v, one, MPFR_RNDD);
	mpfr_div_ui(v, v, 2, MPFR_RNDD);
	mpfr_ui_div(v, 1UL, v, MPFR_RNDD);
	mpfr_add(v, v, one, MPFR_RNDD);
	mpfr_prec_round(v, precision, MPFR_RNDN);
	(void)mpfr_cmp(v, v);
	mpfr_set(v, one, MPFR_RNDD);
	mpfr_exp(v, one, MPFR_RNDD);
	mpfr_sin(v, pi, MPFR_RNDD);
	mpfr_cos(v, pi, MPFR_RNDD);
	mpfr_exp(v, one, MPFR_RNDD);
	mpfr_log(v, pi, MPFR_RNDD);

	mpfr_clear(v);
	mpfr_clear(one);
	mpfr_clear(pi);
	mpfr_free_cache();

	return 0;
}
