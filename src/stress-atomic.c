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

#define DO_ATOMIC_OPS(type, var)			\
{							\
	type tmp = stress_mwc64();			\
							\
	__atomic_store(var, &tmp, __ATOMIC_RELAXED); 	\
	__atomic_load(var, &tmp, __ATOMIC_RELAXED);	\
	__atomic_load(var, &tmp, __ATOMIC_ACQUIRE);	\
	__atomic_add_fetch(var, 1, __ATOMIC_RELAXED);	\
	__atomic_add_fetch(var, 2, __ATOMIC_ACQUIRE);	\
	__atomic_sub_fetch(var, 3, __ATOMIC_RELAXED);	\
	__atomic_sub_fetch(var, 4, __ATOMIC_ACQUIRE);	\
	__atomic_and_fetch(var, ~1, __ATOMIC_RELAXED);	\
	__atomic_and_fetch(var, ~2, __ATOMIC_ACQUIRE);	\
	__atomic_xor_fetch(var, ~4, __ATOMIC_RELAXED);	\
	__atomic_xor_fetch(var, ~8, __ATOMIC_ACQUIRE);	\
	__atomic_or_fetch(var, 16, __ATOMIC_RELAXED);	\
	__atomic_or_fetch(var, 32, __ATOMIC_ACQUIRE);	\
	__atomic_nand_fetch(var, 64, __ATOMIC_RELAXED);	\
	__atomic_nand_fetch(var, 128, __ATOMIC_ACQUIRE);\
	__atomic_clear(var, __ATOMIC_RELAXED);		\
							\
	__atomic_store(var, &tmp, __ATOMIC_RELAXED); 	\
	__atomic_fetch_add(var, 1, __ATOMIC_RELAXED);	\
	__atomic_fetch_add(var, 2, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_sub(var, 3, __ATOMIC_RELAXED);	\
	__atomic_fetch_sub(var, 4, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_and(var, ~1, __ATOMIC_RELAXED);	\
	__atomic_fetch_and(var, ~2, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_xor(var, ~4, __ATOMIC_RELAXED);	\
	__atomic_fetch_xor(var, ~8, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_or(var, 16, __ATOMIC_RELAXED);	\
	__atomic_fetch_or(var, 32, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_nand(var, 64, __ATOMIC_RELAXED);	\
	__atomic_fetch_nand(var, 128, __ATOMIC_ACQUIRE);\
	__atomic_clear(var, __ATOMIC_RELAXED);		\
							\
	__atomic_store(var, &tmp, __ATOMIC_RELAXED); 	\
	__atomic_load(var, &tmp, __ATOMIC_RELAXED);	\
	__atomic_add_fetch(var, 1, __ATOMIC_RELAXED);	\
	__atomic_sub_fetch(var, 3, __ATOMIC_RELAXED);	\
	__atomic_and_fetch(var, ~1, __ATOMIC_RELAXED);	\
	__atomic_xor_fetch(var, ~4, __ATOMIC_RELAXED);	\
	__atomic_or_fetch(var, 16, __ATOMIC_RELAXED);	\
	__atomic_nand_fetch(var, 64, __ATOMIC_RELAXED);	\
	__atomic_load(var, &tmp, __ATOMIC_ACQUIRE);	\
	__atomic_add_fetch(var, 2, __ATOMIC_ACQUIRE);	\
	__atomic_sub_fetch(var, 4, __ATOMIC_ACQUIRE);	\
	__atomic_and_fetch(var, ~2, __ATOMIC_ACQUIRE);	\
	__atomic_xor_fetch(var, ~8, __ATOMIC_ACQUIRE);	\
	__atomic_or_fetch(var, 32, __ATOMIC_ACQUIRE);	\
	__atomic_nand_fetch(var, 128, __ATOMIC_ACQUIRE);\
	__atomic_clear(var, __ATOMIC_RELAXED);		\
							\
	__atomic_store(var, &tmp, __ATOMIC_RELAXED); 	\
	__atomic_fetch_add(var, 1, __ATOMIC_RELAXED);	\
	__atomic_fetch_sub(var, 3, __ATOMIC_RELAXED);	\
	__atomic_fetch_and(var, ~1, __ATOMIC_RELAXED);	\
	__atomic_fetch_xor(var, ~4, __ATOMIC_RELAXED);	\
	__atomic_fetch_or(var, 16, __ATOMIC_RELAXED);	\
	__atomic_fetch_nand(var, 64, __ATOMIC_RELAXED);	\
	__atomic_fetch_add(var, 2, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_sub(var, 4, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_and(var, ~2, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_xor(var, ~8, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_or(var, 32, __ATOMIC_ACQUIRE);	\
	__atomic_fetch_nand(var, 128, __ATOMIC_ACQUIRE);\
	__atomic_clear(var, __ATOMIC_RELAXED);		\
}

static const stress_help_t help[] = {
	{ NULL,	"atomic",	"start N workers exercising GCC atomic operations" },
	{ NULL, "atomic-ops",	"stop after N bogo atomic bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(TEST_ATOMIC_BUILD)

uint64_t stress_mwc64(void)
{
	static uint64_t v = 0xdeadbeef;

	v = (v >> 1) ^ (v << 63);

	return v;
}

int main(void)
{
	uint64_t val64;
	uint32_t val32;
	uint16_t val16;
	uint8_t  val8;

	DO_ATOMIC_OPS(uint64_t, &val64);
	DO_ATOMIC_OPS(uint32_t, &val32);
	DO_ATOMIC_OPS(uint16_t, &val16);
	DO_ATOMIC_OPS(uint8_t, &val8);

	return 0;
}

#else
#if defined(HAVE_ATOMIC)

/*
 *  stress_atomic()
 *      stress gcc atomic memory ops
 */
static int stress_atomic(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		DO_ATOMIC_OPS(uint64_t, &g_shared->atomic.val64);
		DO_ATOMIC_OPS(uint32_t, &g_shared->atomic.val32);
		DO_ATOMIC_OPS(uint16_t, &g_shared->atomic.val16);
		DO_ATOMIC_OPS(uint8_t, &g_shared->atomic.val8);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_atomic_info = {
	.stressor = stress_atomic,
	.class = CLASS_CPU | CLASS_MEMORY,
	.help = help
};

#else
stressor_info_t stress_atomic_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_MEMORY,
	.help = help
};
#endif
#endif
