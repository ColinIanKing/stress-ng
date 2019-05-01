/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"rdrand N",	"start N workers exercising rdrand (x86 only)" },
	{ NULL,	"rdrand-ops N",	"stop after N rdrand bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_X86) &&	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)

static bool rdrand_supported = false;

/*
 *  stress_rdrand_supported()
 *	check if rdrand is supported
 */
static int stress_rdrand_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	if (!cpu_is_x86()) {
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
static inline uint64_t rdrand64(void)
{
	uint64_t        ret;

	asm volatile("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}

/*
 *  Unrolled 32 times
 */
#define RDRAND64x32()	\
{			\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
}
#else
/*
 *  rdrand32()
 *	read 32 bit random value
 */
static inline uint32_t rdrand32(void)
{
	uint32_t        ret;

	asm volatile("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}

/*
 *  Unrolled 64 times
 */
#define RDRAND32x64()	\
{			\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
	rdrand32();	\
}
#endif

/*
 *  stress_rdrand()
 *      stress Intel rdrand instruction
 */
static int stress_rdrand(const args_t *args)
{
	if (rdrand_supported) {
		double time_start, duration, billion_bits;
		bool lock = false;

		time_start = time_now();
		do {
#if defined(__x86_64__) || defined(__x86_64)
			RDRAND64x32();
#else
			RDRAND32x64();
#endif
			inc_counter(args);
		} while (keep_stressing());

		duration = time_now() - time_start;
		billion_bits = ((double)get_counter(args) * 64.0 * 32.0) / 1000000000.0;

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
