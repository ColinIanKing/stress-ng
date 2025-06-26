/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-asm-x86.h"
#include "core-asm-ppc64.h"
#include "core-builtin.h"
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

static const stress_opt_t opts[] = {
	{ OPT_rdrand_seed, "rdrand-seed", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
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

/*
 *  rdrand64()
 *	read 64 bit random value
 */
static inline uint64_t rand64(void)
{
	return stress_asm_x86_rdrand();
}

/*
 *  rdseed64()
 *	read 64 bit random value
 */
static inline uint64_t seed64(void)
{
	return stress_asm_x86_rdseed();
}
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
#endif
#if defined(HAVE_BUILTIN_CPU_IS_POWER10)
	if (__builtin_cpu_is("power10")) {
		rdrand_supported = true;
		return 0;
	}
#endif
#if defined(HAVE_BUILTIN_CPU_IS_POWER11)
	if (__builtin_cpu_is("power11")) {
		rdrand_supported = true;
		return 0;
	}
#endif
	pr_inf_skip("%s stressor will be skipped, cannot detect if the CPU "
		"supports the instruction 'darn'\n", name);
	return -1;
}

static inline uint64_t rand64(void)
{
	return (uint64_t)(stress_asm_ppc64_darn() << 32) | (uint64_t)stress_asm_ppc64_darn();
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

static int stress_rdrand_sane(stress_args_t *args)
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
static int stress_rdrand(stress_args_t *args)
{
	double average;
	uint64_t lo, hi;
	bool out_of_range;
	int rc = EXIT_SUCCESS;
	size_t j;
#if defined(HAVE_SEED_CAPABILITY)
	bool rdrand_seed = false;
#endif
	static uint64_t ALIGN64 counters[16];

	(void)shim_memset(counters, 0, sizeof(counters));
#if defined(HAVE_SEED_CAPABILITY)
	(void)stress_get_setting("rdrand-seed", &rdrand_seed);
#endif

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_X86_RDRAND) &&	\
    defined(HAVE_ASM_X86_RDSEED) &&	\
    defined(HAVE_SEED_CAPABILITY)
	if (rdrand_seed && !stress_cpu_x86_has_rdseed()) {
		pr_inf("rdrand-seed ignored, cpu does not support feature, defaulting to rdrand\n");
		rdrand_seed = false;
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (rdrand_supported) {
		double time_start, duration, million_bits, rate;
		register int i;
		uint64_t c;

		rc = stress_rdrand_sane(args);

		time_start = stress_time_now();

#if defined(HAVE_SEED_CAPABILITY)
		if (rdrand_seed) {
			do {
				for (i = 0; i < 64; i++) {
					register uint64_t r;

					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()
					SEED64x32()

					r = seed64();
					counters[r & 0xf]++;
					counters[(r >> 13) & 0xf]++;
					counters[(r >> 29) & 0xf]++;
					counters[(r >> 52) & 0xf]++;
				}
				stress_bogo_add(args, (uint64_t)i);
			} while (stress_continue(args));
		} else
#endif
		{
			do {
				/* 64 rounds of (32 * 8) + 1 random reads */
				for (i = 0; i < 64; i++) {
					register uint64_t r;

					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()
					RAND64x32()

					r = rand64();
					counters[r & 0xf]++;
					counters[(r >> 13) & 0xf]++;
					counters[(r >> 29) & 0xf]++;
					counters[(r >> 52) & 0xf]++;
				}
				stress_bogo_add(args, (uint64_t)i);
			} while (stress_continue(args));
		}

		duration = stress_time_now() - time_start;
		c = stress_bogo_get(args);
		million_bits = ((double)c * 64.0 * 257.0) * ONE_MILLIONTH;
		rate = (duration > 0.0) ? million_bits / duration : 0.0;
		stress_metrics_set(args, 0, "million random bits read",
			million_bits, STRESS_METRIC_GEOMETRIC_MEAN);
		stress_metrics_set(args, 1, "million random bits per sec",
			rate, STRESS_METRIC_HARMONIC_MEAN);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (average = 0.0, j = 0; j < SIZEOF_ARRAY(counters); j++)
		average += (double)counters[j];

	/*
	 *  If we have a reasonable number of samples then check
	 *  for a poor random distribution
	 */
	average /= (double)SIZEOF_ARRAY(counters);
	if (average > 10000.0) {
		double total = 0.0;

		lo = (uint64_t)average - (average * 0.05);
		hi = (uint64_t)average + (average * 0.05);
		out_of_range = false;

		for (j = 0; j < SIZEOF_ARRAY(counters); j++) {
			total += counters[j];
			if ((counters[j] < lo) || (counters[j] > hi)) {
				out_of_range = true;
				rc = EXIT_FAILURE;
			}
		}
		if (out_of_range) {
			pr_fail("%s: poor distribution of random values\n", args->name);
			if (stress_instance_zero(args)) {
				uint64_t i;
				const uint64_t shift = 1ULL << 60;

				pr_inf("Frequency distribution:\n");
				for (i = 0; i < (uint64_t)SIZEOF_ARRAY(counters); i++) {
					pr_inf("0x%16.16" PRIx64 "..0x%16.16" PRIx64 " %5.2f%% %10" PRIu64 "\n",
						i  * shift, ((i + 1) * shift) - 1, counters[i] * 100.0 / total, counters[i]);
				}
			}
		}
	}

	return rc;
}

const stressor_info_t stress_rdrand_info = {
	.stressor = stress_rdrand,
	.supported = stress_rdrand_supported,
	.opts = opts,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else

static int stress_rdrand_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, CPU "
		"does not support the rdrand instruction.\n", name);
	return -1;
}

const stressor_info_t stress_rdrand_info = {
	.stressor = stress_unimplemented,
	.supported = stress_rdrand_supported,
	.opts = opts,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "x86 CPU only, built without rdrand or rdseed opcode support"
};
#endif
