/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2022 Colin Ian King
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
#include "core-cpu.h"

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"rdrand N",	"start N workers exercising rdrand (x86 only)" },
	{ NULL,	"rdrand-ops N",	"stop after N rdrand bogo operations" },
	{ NULL, "rdrand-seed",	"use rdseed instead of rdrand" },
	{ NULL,	NULL,		NULL }
};

#define STRESS_SANE_LOOPS_QUICK	16
#define STRESS_SANE_LOOPS	65536

static int stress_set_rdrand_seed(const char *opt)
{
	(void)opt;

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_X86_RDRAND) &&	\
    defined(HAVE_ASM_X86_RDSEED)
	if (stress_cpu_x86_has_rdseed()) {
		bool rdrand_seed = true;

		return stress_set_setting("rdrand-seed", TYPE_ID_BOOL, &rdrand_seed);
	}
#endif
	pr_inf("rdrand-seed ignored, cpu does not support feature, defaulting to rdrand\n");
	return 0;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_rdrand_seed,	stress_set_rdrand_seed },
	{ 0,			NULL }
};

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_RDRAND)

#define HAVE_RAND_CAPABILITY
#if defined(HAVE_ASM_X86_RDSEED)
#define HAVE_SEED_CAPABILITY
#endif

static bool rdrand_supported = false;

/*
 *  stress_rdrand_supported()
 *	check if rdrand is supported
 */
static int stress_rdrand_supported(const char *name)
{
	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf_skip("%s stressor will be skipped, "
			"not a recognised Intel CPU\n", name);
		return -1;
	}

	if (!stress_cpu_x86_has_rdrand()) {
		pr_inf_skip("%s stressor will be skipped, CPU "
			"does not support the rdrand instruction\n", name);
		return -1;
	}
	rdrand_supported = true;
	return 0;
}

#if defined(__x86_64__) || defined(__x86_64)
/*
 *  rdrand64()
 *	read 64 bit random value
 */
static inline uint64_t rand64(void)
{
	uint64_t        ret;

	__asm__ __volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}

/*
 *  rdseed64()
 *	read 64 bit random value
 */
static inline uint64_t seed64(void)
{
	uint64_t        ret;

	__asm__ __volatile__("1:;\n\
	rdseed %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}
#else
/*
 *  rdrand64()
 *	read 2 x 32 bit random value
 */
static inline uint32_t rand64(void)
{
	uint32_t        ret;

	__asm__ volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	__asm__ __volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}

/*
 *  seed64()
 *	read 2 x 32 bit random value
 */
static inline uint32_t seed64(void)
{
	uint32_t        ret;

	__asm__ __volatile__("1:;\n\
	rdseed %0;\n\
	jnc 1b;\n":"=r"(ret));

	__asm__ __volatile__("1:;\n\
	rdseed %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}
#endif
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DARN)

#define HAVE_RAND_CAPABILITY

static bool rdrand_supported = false;
static volatile uint64_t v;

static int stress_rdrand_supported(const char *name)
{
#if defined(HAVE_BUILTIN_CPU_IS_POWER9)
	if (__builtin_cpu_is("power9")) {
		rdrand_supported = true;
		return 0;
	}
	pr_inf_skip("%s stressor will be skipped, CPU "
		"does not support the instruction 'darn'\n", name);
	return -1;
#else
	pr_inf_skip("%s stressor will be skipped, cannot"
		"determine if CPU is a power9 the instruction 'darn'\n", name);
	return -1;
#endif
}

static inline uint64_t rand64(void)
{
	uint64_t val;

	/* Unconditioned raw deliver a raw number */
	__asm__ __volatile__("darn %0, 0\n" : "=r"(val) :);
	return val;
}
#endif

#if defined(HAVE_RAND_CAPABILITY)

/*
 *  Unrolled 32 times
 */
#define RAND64x32()	\
{			\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
	rand64();	\
}

#if defined(HAVE_SEED_CAPABILITY)
/*
 *  Unrolled 32 times
 */
#define SEED64x32()	\
{			\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
	seed64();	\
}
#endif

static int stress_rdrand_sane(const stress_args_t *args)
{
	const uint64_t r1 = rand64();
	int i, changed, same;

	for (changed = 0, i = 0; i < STRESS_SANE_LOOPS_QUICK; i++) {
		const uint64_t r2 = rand64();

		if (r1 != r2)
			changed++;
	}

	/*
	 *  random 64 bit reads locked up and all the same?
	 */
	if (changed == 0) {
		pr_fail("%s: random value did not change in %d reads\n",
			args->name, STRESS_SANE_LOOPS_QUICK);
		return EXIT_FAILURE;
	}

	/*
	 *  If STRESS_SANE_LOOPS is small, then it's unlikely (but not
	 *  impossible) that we read the same 64 bit random data multiple
	 *  times
	 */
	for (same = 0, i = 0; i < STRESS_SANE_LOOPS; i++) {
		const uint64_t r2 = rand64();

		if (r1 == r2)
			same++;
	}

	/*  Not a failure, but it is worth reporting */
	if (same > 0) {
		pr_inf("%s: 64 bit random value was the same in %d of %d reads (should be quite unlikely)\n",
			args->name,
			same, STRESS_SANE_LOOPS);
	}

	return EXIT_SUCCESS;
}

/*
 *  stress_rdrand()
 *      stress Intel rdrand instruction
 */
static int stress_rdrand(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
#if defined(HAVE_SEED_CAPABILITY)
	bool rdrand_seed = false;

	(void)stress_get_setting("rdrand-seed", &rdrand_seed);
#endif

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (rdrand_supported) {
		double time_start, duration, billion_bits;
		bool lock = false;
		register int i;

		rc = stress_rdrand_sane(args);

		time_start = stress_time_now();

#if defined(HAVE_SEED_CAPABILITY)
		if (rdrand_seed) {
			do {
				for (i = 0; i < 4096; i++) {
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
				}
				add_counter(args, i);
			} while (keep_stressing(args));
		} else
#endif
		{
			do {
				for (i = 0; i < 4096; i++) {
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
				}
				add_counter(args, i);
			} while (keep_stressing(args));
		}

		duration = stress_time_now() - time_start;
		billion_bits = ((double)get_counter(args) * 64.0 * 256.0) * ONE_BILLIONTH;

		pr_lock(&lock);
		pr_dbg_lock(&lock, "%s: %.3f billion random bits read "
			"(instance %" PRIu32")\n",
			args->name, billion_bits, args->instance);
		if (duration > 0.0) {
			pr_dbg_lock(&lock, "%s: %.3f billion random bits per "
				"second (instance %" PRIu32")\n",
				args->name,
				(double)billion_bits / duration,
				args->instance);
		}
		pr_unlock(&lock);
		stress_misc_stats_set(args->misc_stats, 0, "billion random bits read", billion_bits);
		stress_misc_stats_set(args->misc_stats, 1, "billion random bits / sec", billion_bits / duration);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_rdrand_info = {
	.stressor = stress_rdrand,
	.supported = stress_rdrand_supported,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_rdrand_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, CPU "
		"does not support the rdrand instruction.\n", name);
	return -1;
}

stressor_info_t stress_rdrand_info = {
	.stressor = stress_not_implemented,
	.supported = stress_rdrand_supported,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_CPU,
	.help = help
};
#endif
