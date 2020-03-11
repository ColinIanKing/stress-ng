/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"rdrand N",	"start N workers exercising rdrand (x86 only)" },
	{ NULL,	"rdrand-ops N",	"stop after N rdrand bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)

#define HAVE_RAND_CAPABILITY

static bool rdrand_supported = false;

/*
 *  stress_rdrand_supported()
 *	check if rdrand is supported
 */
static int stress_rdrand_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf("rdrand stressor will be skipped, "
			"not a recognised Intel CPU\n");
		return -1;
	}
	/* ..and supports rdrand? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(ecx & 0x40000000)) {
		pr_inf("rdrand stressor will be skipped, CPU "
			"does not support the rdrand instruction\n");
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

	asm volatile("1:;\n\
	rdrand %0;\n\
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

	asm volatile("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	asm volatile("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}
#endif
#endif


#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_DARN)

#define HAVE_RAND_CAPABILITY

static bool rdrand_supported = false;
static volatile uint64_t v;

static int stress_rdrand_supported(void)
{
#if defined(HAVE_BUILTIN_CPU_IS)
	if (__builtin_cpu_is("power9")) {
		rdrand_supported = true;
		return 0;
	}
	pr_inf("rdrand stressor will be skipped, CPU "
		"does not support the instuction 'darn'\n");
	return -1;
#else
	pr_inf("rdrand stressor will be skipped, cannot"
		"determine if CPU is a power9 the instruction 'darn'\n");
	return -1;
#endif
}

static inline uint64_t rand64(void)
{
	uint64_t val;

	/* Unconditioned raw deliver a raw number */
	asm volatile("darn %0, 0\n" : "=r"(val) :);
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

#define RAND64x256()	\
{			\
}

/*
 *  stress_rdrand()
 *      stress Intel rdrand instruction
 */
static int stress_rdrand(const stress_args_t *args)
{
	if (rdrand_supported) {
		double time_start, duration, billion_bits;
		bool lock = false;

		time_start = stress_time_now();
		do {
			register int i;

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
		} while (keep_stressing());

		duration = stress_time_now() - time_start;
		billion_bits = ((double)get_counter(args) * 64.0 * 256.0) / 1000000000.0;

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
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_rdrand_info = {
	.stressor = stress_rdrand,
	.supported = stress_rdrand_supported,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_rdrand_supported(void)
{
	pr_inf("rdrand stressor will be skipped, CPU "
		"does not support the rdrand instruction.\n");
	return -1;
}

stressor_info_t stress_rdrand_info = {
	.stressor = stress_not_implemented,
	.supported = stress_rdrand_supported,
	.class = CLASS_CPU,
	.help = help
};
#endif
