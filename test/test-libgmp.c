/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#include <stdio.h>
#include <gmp.h>

/* The following functions from libgmp are used by stress-ng */

static void *gmp_funcs[] = {
	mpz_cdiv_qr,
	mpz_clears,
	mpz_cmp,
	/* mpz_cmp_ui, */
	mpz_inits,
	mpz_mul,
	mpz_nextprime,
	mpz_set_ui,
	mpz_sizeinbase,
	mpz_sqrt,
};

int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(gmp_funcs) / sizeof(gmp_funcs[0]); i++)
		printf("%p\n", gmp_funcs[i]);
	return 0;
}
