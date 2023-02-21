/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-pragma.h"

static const stress_help_t help[] = {
	{ NULL,	"x86cpuid N",		"start N workers exercising the x86 cpuid instruction" },
	{ NULL,	"x86cpuid-ops N",	"stop after N cpuid bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	uint32_t	eax;
	uint32_t	ecx;
} stress_cpuid_regs_t;

static const stress_cpuid_regs_t ALIGN64 stress_cpuid_regs[] = {
	{ 0x00000000, 0x00000000 },	/* Highest Function Parameter and Manufacturer ID */
	{ 0x00000001, 0x00000000 },	/* Processor Info and Feature Bits */
	{ 0x00000002, 0x00000000 },	/* Cache and TLB Descriptor information */
	{ 0x00000003, 0x00000000 },	/* Processor Serial Number */
	{ 0x00000004, 0x00000000 },	/* Intel thread/core and cache topology */
	{ 0x00000006, 0x00000000 },	/* Thermal and power management */
	{ 0x00000007, 0x00000000 },	/* Extended Features */
	{ 0x00000007, 0x00000001 },	/* Extended Features */
	{ 0x00000009, 0x00000000 },	/* Direct Cache Access Information Leaf */
	{ 0x0000000a, 0x00000000 },	/* Architectural Performance Monitoring Leaf */
	{ 0x0000000b, 0x00000000 },	/* Extended Topology Enumeration Leaf */
	{ 0x0000000d, 0x00000000 },	/* Processor Extended State Enumeration Main Leaf */
	{ 0x0000000d, 0x00000000 },	/* Processor Extended State Enumeration Sub-leaf */
	{ 0x0000000f, 0x00000000 },	/* Intel Resource Director Technology (Intel RDT) Monitoring Enumeration Sub-leaf */
	{ 0x0000000f, 0x00000001 },	/* L3 Cache Intel RDT Monitoring Capability Enumeration Sub-leaf */
	{ 0x00000010, 0x00000000 },	/* Intel Resource Director Technology (Intel RDT) Allocation Enumeration Sub-leaf */
	{ 0x00000010, 0x00000001 },	/* L3 Cache Allocation Technology Enumeration Sub-leaf */
	{ 0x00000010, 0x00000002 },	/* L3 Cache Allocation Technology Enumeration Sub-leaf */
	{ 0x00000010, 0x00000003 },	/* Memory Bandwidth Allocation Enumeration Sub-leaf */
	{ 0x00000012, 0x00000000 },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 0 */
	{ 0x00000012, 0x00000001 },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 1 */
	{ 0x00000012, 0x00000001 },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 1 */
	{ 0x00000014, 0x00000000 },	/* Intel Processor Trace Enumeration Main Leaf */
	{ 0x00000014, 0x00000001 },	/* Intel Processor Trace Enumeration Sub-leaf */
	{ 0x00000015, 0x00000000 },	/* Time Stamp Counter and Nominal Core Crystal Clock Information Leaf */
	{ 0x00000016, 0x00000000 },	/* Processor Frequency Information Leaf */
	{ 0x00000017, 0x00000000 },	/* System-On-Chip Vendor Attribute Enumeration Main Leaf */
	{ 0x00000017, 0x00000001 },	/* System-On-Chip Vendor Attribute Enumeration Sub-leaf 0 */
	{ 0x00000017, 0x00000002 },	/* System-On-Chip Vendor Attribute Enumeration Sub-Leaf 1 */
	{ 0x00000017, 0x00000003 },	/* System-On-Chip Vendor Attribute Enumeration Sub-Leaf 2 */
	{ 0x00000018, 0x00000000 },	/* Deterministic Address Translation Parameters Main Leaf */
	{ 0x00000018, 0x00000001 },	/* Deterministic Address Translation Parameters Sub-Leaf 0 */
	{ 0x00000019, 0x00000000 },	/* Key Locker Leaf */
	{ 0x0000001a, 0x00000000 },	/* Hybrid Information Enumeration Leaf */
	{ 0x0000001b, 0x00000000 },	/* PCONFIG Information Sub-leaf 0 */
	{ 0x0000001c, 0x00000000 },	/* Last Branch Records Information Leaf */
	{ 0x0000001f, 0x00000000 },	/* V2 Extended Topology Enumeration Leaf */
	{ 0x80000000, 0x00000000 },	/* Extended Function CPUID Information */
	{ 0x80000001, 0x00000000 },	/* Extended Processor Signature and Feature Bits */
	{ 0x80000002, 0x00000000 },	/* Processor Brand String */
	{ 0x80000003, 0x00000000 },	/* Processor brand string */
	{ 0x80000004, 0x00000000 },	/* Processor brand string */
	{ 0x80000005, 0x00000000 },	/* L1 Cache and TLB Identifiers */
	{ 0x80000006, 0x00000000 },	/* Extended L2 Cache Features */
	{ 0x80000007, 0x00000000 },	/* Advanced Power Management information */
	{ 0x80000008, 0x00000000 },	/* Virtual and Physical address size */
	{ 0x8000000a, 0x00000000 },	/* get SVM information */
	{ 0x80000019, 0x00000000 },	/* get TLB configuration descriptors */
	{ 0x8000001a, 0x00000000 },	/* get performance optimization identifiers */
	{ 0x8000001b, 0x00000000 },	/* get IBS information */
	{ 0x8000001c, 0x00000000 },	/* get LWP information */
	{ 0x8000001d, 0x00000000 },	/* get cache configuration descriptors */
	{ 0x8000001e, 0x00000000 },	/* get APIC/unit/node information */
	{ 0x8000001f, 0x00000000 },	/* get SME/SEV information */
};

static void OPTIMIZE3 stress_x86cpuid_reorder_regs(const size_t n, stress_cpuid_regs_t *reordered_cpu_regs)
{
	uint8_t ALIGN64 index[SIZEOF_ARRAY(stress_cpuid_regs)];
	register size_t i;

	for (i = 0; i < n; i++)
		index[i] = i;

	for (i = 0; i < n; i++) {
		register const size_t j = stress_mwc8modn((uint8_t)n);
		register uint8_t tmp;

		tmp = index[i];
		index[i] = index[j];
		index[j] = tmp;
	}

	for (i = 0; i < n; i++)
		reordered_cpu_regs[i] = stress_cpuid_regs[index[i]];
}

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
	const size_t n = SIZEOF_ARRAY(stress_cpuid_regs);

	stress_cpuid_regs_t ALIGN64 reordered_cpu_regs[SIZEOF_ARRAY(stress_cpuid_regs)];

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	do {
		uint32_t eax, ebx, ecx, edx;
		double t = stress_time_now();
		register size_t i, j;

		stress_x86cpuid_reorder_regs(n, reordered_cpu_regs);

		for (j = 0; j < 1024; j++) {
PRAGMA_UNROLL_N(8)
			for (i = 0; i < n; i++) {
				eax = reordered_cpu_regs[i].eax;
				ebx = 0; /* Not required */
				ecx = reordered_cpu_regs[i].ecx;
				edx = 0; /* Not required */

				stress_asm_x86_cpuid(eax, ebx, ecx, edx);
			}
			inc_counter(args);
		}
		duration += stress_time_now() - t;
		count += (double)n * (double)j;
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per cpuid instruction", STRESS_DBL_NANOSECOND * rate);

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
