/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-put.h"

static const stress_help_t help[] = {
	{ NULL,	"x86cpuid N",		"start N workers exercising the x86 cpuid instruction" },
	{ NULL,	"x86cpuid-ops N",	"stop after N cpuid bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(STRESS_ARCH_X86)
/*
 *  stress_x86cpuid()
 *	get CPU id info, x86 only
 *	see https://en.wikipedia.org/wiki/CPUID
 *	and https://www.sandpile.org/x86/cpuid.htm
 */
static int stress_x86cpuid(const stress_args_t *args)
{
	double count = 0.0, duration = 0.0, rate;
	do {
		uint32_t eax, ebx, ecx, edx;
		double t = stress_time_now();

		/*  Highest Function Parameter and Manufacturer ID */
		eax = 0;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Processor Info and Feature Bits */
		eax = 1;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/*  Cache and TLB Descriptor information */
		eax = 2;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Processor Serial Number */
		eax = 3;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Intel thread/core and cache topology */
		eax = 4;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Thermal and power management */
		eax = 6;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Extended Features */
		eax = 7;
		ebx = 0; /* Not required */
		ecx = 0; /* Must be 0 */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Extended Features */
		eax = 7;
		ebx = 0; /* Not required */
		ecx = 1; /* Must be 1 */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Intel thread/core and cache topology */
		eax = 0xb;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Get highest extended function index */
		eax = 0x80000000;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Extended processor info */
		eax = 0x80000001;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Processor brand string */
		eax = 0x80000002;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Processor brand string */
		eax = 0x80000003;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Processor brand string */
		eax = 0x80000004;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* L1 Cache and TLB Identifiers */
		eax = 0x80000005;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Extended L2 Cache Features */
		eax = 0x80000006;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Advanced Power Management information */
		eax = 0x80000007;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* Virtual and Physical address size */
		eax = 0x80000008;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get SVM information */
		eax = 0x8000000a;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get TLB configuration descriptors */
		eax = 0x80000019;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get performance optimization identifiers */
		eax = 0x8000001a;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get IBS information */
		eax = 0x8000001a;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get LWP information */
		eax = 0x8000001c;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get cache configuration descriptors */
		eax = 0x8000001d;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get APIC/unit/node information */
		eax = 0x8000001e;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		/* get SME/SEV information */
		eax = 0x8000001f;
		ebx = 0; /* Not required */
		ecx = 0; /* Not required */
		edx = 0; /* Not required */
		stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

		duration += stress_time_now() - t;
		count += 26.0;

		inc_counter(args);
	} while (keep_stressing(args));

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "nanosecs per cpuid instruction", 1000000000.0 * rate);

	return EXIT_SUCCESS;
}

stressor_info_t stress_x86cpuid_info = {
	.stressor = stress_x86cpuid,
	.class = CLASS_CPU,
	.verify = VERIFY_NONE,
	.help = help
};
#else

stressor_info_t stress_x86cpuid_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without x86 cpuid instruction support"
};
#endif
