/*
 * Copyright (C) 2022-2025 Colin Ian King.
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

#if defined(STRESS_ARCH_X86)

typedef struct {
	uint32_t	eax;
	uint32_t	ecx;
	bool 		verify;
} stress_cpuid_regs_t;

typedef struct {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
} stress_cpuid_saved_regs_t;

static const stress_cpuid_regs_t ALIGN64 stress_cpuid_regs[] = {
	{ 0x00000000, 0x00000000, true },	/* Highest Function Parameter and Manufacturer ID */
	{ 0x00000001, 0x00000000, false },	/* Processor Info and Feature Bits */
	{ 0x00000002, 0x00000000, false },	/* Cache and TLB Descriptor information */
	{ 0x00000003, 0x00000000, true },	/* Processor Serial Number */
	{ 0x00000004, 0x00000000, false },	/* Intel thread/core and cache topology */
	{ 0x00000006, 0x00000000, false },	/* Thermal and power management */
	{ 0x00000007, 0x00000000, true },	/* Extended Features */
	{ 0x00000007, 0x00000001, true },	/* Extended Features */
	{ 0x00000009, 0x00000000, true },	/* Direct Cache Access Information Leaf */
	{ 0x0000000a, 0x00000000, true },	/* Architectural Performance Monitoring Leaf */
	{ 0x0000000b, 0x00000000, false },	/* Extended Topology Enumeration Leaf */
	{ 0x0000000d, 0x00000000, true },	/* Processor Extended State Enumeration Main Leaf */
	{ 0x0000000d, 0x00000000, true },	/* Processor Extended State Enumeration Sub-leaf */
	{ 0x0000000f, 0x00000000, true },	/* Intel Resource Director Technology (Intel RDT) Monitoring Enumeration Sub-leaf */
	{ 0x0000000f, 0x00000001, false },	/* L3 Cache Intel RDT Monitoring Capability Enumeration Sub-leaf */
	{ 0x00000010, 0x00000000, false },	/* Intel Resource Director Technology (Intel RDT) Allocation Enumeration Sub-leaf */
	{ 0x00000010, 0x00000001, false },	/* L3 Cache Allocation Technology Enumeration Sub-leaf */
	{ 0x00000010, 0x00000002, false },	/* L3 Cache Allocation Technology Enumeration Sub-leaf */
	{ 0x00000010, 0x00000003, false },	/* Memory Bandwidth Allocation Enumeration Sub-leaf */
	{ 0x00000012, 0x00000000, false },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 0 */
	{ 0x00000012, 0x00000001, false },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 1 */
	{ 0x00000012, 0x00000001, false },	/* Intel SGX Capability Enumeration Leaf, sub-leaf 1 */
	{ 0x00000014, 0x00000000, false },	/* Intel Processor Trace Enumeration Main Leaf */
	{ 0x00000014, 0x00000001, false },	/* Intel Processor Trace Enumeration Sub-leaf */
	{ 0x00000015, 0x00000000, false },	/* Time Stamp Counter and Nominal Core Crystal Clock Information Leaf */
	{ 0x00000016, 0x00000000, false },	/* Processor Frequency Information Leaf */
	{ 0x00000017, 0x00000000, false },	/* System-On-Chip Vendor Attribute Enumeration Main Leaf */
	{ 0x00000017, 0x00000001, false },	/* System-On-Chip Vendor Attribute Enumeration Sub-leaf 0 */
	{ 0x00000017, 0x00000002, false },	/* System-On-Chip Vendor Attribute Enumeration Sub-Leaf 1 */
	{ 0x00000017, 0x00000003, false },	/* System-On-Chip Vendor Attribute Enumeration Sub-Leaf 2 */
	{ 0x00000018, 0x00000000, false },	/* Deterministic Address Translation Parameters Main Leaf */
	{ 0x00000018, 0x00000001, false },	/* Deterministic Address Translation Parameters Sub-Leaf 0 */
	{ 0x00000019, 0x00000000, false },	/* Key Locker Leaf */
	{ 0x0000001a, 0x00000000, false },	/* Hybrid Information Enumeration Leaf */
	{ 0x0000001b, 0x00000000, false },	/* PCONFIG Information Sub-leaf 0 */
	{ 0x0000001c, 0x00000000, false },	/* Last Branch Records Information Leaf */
	{ 0x0000001d, 0x00000000, false },	/* Tile Information */
	{ 0x0000001e, 0x00000000, false },	/* TMUL Information */
	{ 0x0000001e, 0x00000001, false },	/* TMUL Information, feature flags */
	{ 0x0000001f, 0x00000000, false },	/* V2 Extended Topology Enumeration Leaf */
	{ 0x00000024, 0x00000000, false },	/* AVX10 Converged Vector ISA Leaf */
	{ 0x00000024, 0x00000001, false },	/* Discrete AVX10 Features */
	{ 0x20000000, 0x00000000, false },	/* Highest Xeon Phi Function Implemented */
	{ 0x20000001, 0x00000000, false },	/* Xeon Phi Feature Bits */
	{ 0x40000000, 0x00000000, false },	/* Hypervisor ID string */
	{ 0x80000000, 0x00000000, false },	/* Extended Function CPUID Information */
	{ 0x80000001, 0x00000000, false },	/* Extended Processor Signature and Feature Bits */
	{ 0x80000002, 0x00000000, false },	/* Processor Brand String */
	{ 0x80000003, 0x00000000, false },	/* Processor brand string */
	{ 0x80000004, 0x00000000, false },	/* Processor brand string */
	{ 0x80000005, 0x00000000, false },	/* L1 Cache and TLB Identifiers */
	{ 0x80000006, 0x00000000, false },	/* Extended L2 Cache Features */
	{ 0x80000007, 0x00000000, false },	/* Advanced Power Management information */
	{ 0x80000008, 0x00000000, false },	/* Virtual and Physical address size */
	{ 0x8000000a, 0x00000000, false },	/* get SVM information */
	{ 0x80000019, 0x00000000, false },	/* get TLB configuration descriptors */
	{ 0x8000001a, 0x00000000, false },	/* get performance optimization identifiers */
	{ 0x8000001b, 0x00000000, false },	/* get IBS information */
	{ 0x8000001c, 0x00000000, false },	/* get LWP information */
	{ 0x8000001d, 0x00000000, false },	/* get cache configuration descriptors */
	{ 0x8000001e, 0x00000000, false },	/* get APIC/unit/node information */
	{ 0x8000001f, 0x00000000, false },	/* get SME/SEV information */
	{ 0x80000021, 0x00000000, false },	/* Extended Feature Identification 2 */
	{ 0x8fffffff, 0x00000000, false },	/* AMD Easter Egg */
	{ 0xc0000000, 0x00000000, false },	/* Highest Centaur Extended Function */
	{ 0xc0000001, 0x00000000, false },	/* Centaur Feature Information */
};

static void OPTIMIZE3 stress_x86cpuid_reorder_regs(const size_t n, stress_cpuid_regs_t *reordered_cpu_regs)
{
	uint8_t ALIGN64 idx[SIZEOF_ARRAY(stress_cpuid_regs)];
	register size_t i;

	for (i = 0; i < n; i++)
		idx[i] = i;

	for (i = 0; i < n; i++) {
		register const size_t j = stress_mwc8modn((uint8_t)n);
		register uint8_t tmp;

		tmp = idx[i];
		idx[i] = idx[j];
		idx[j] = tmp;
	}

	for (i = 0; i < n; i++)
		reordered_cpu_regs[i] = stress_cpuid_regs[idx[i]];
}

/*
 *  stress_x86cpuid()
 *	get CPU id info, x86 only
 *	see https://en.wikipedia.org/wiki/CPUID
 *	and https://www.sandpile.org/x86/cpuid.htm
 */
static int stress_x86cpuid(stress_args_t *args)
{
	double count = 0.0, duration = 0.0, rate;
	const size_t n = SIZEOF_ARRAY(stress_cpuid_regs);
	stress_cpuid_saved_regs_t saved_regs[SIZEOF_ARRAY(stress_cpuid_regs)];
	int rc = EXIT_SUCCESS;

	stress_cpuid_regs_t ALIGN64 reordered_cpu_regs[SIZEOF_ARRAY(stress_cpuid_regs)];

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;
		register size_t i, j;

		stress_x86cpuid_reorder_regs(n, reordered_cpu_regs);

		for (i = 0; i < n; i++) {
			if (stress_cpuid_regs[i].verify) {
				uint32_t eax, ebx, ecx, edx;

				eax = stress_cpuid_regs[i].eax;
				ebx = 0; /* Not required */
				ecx = stress_cpuid_regs[i].ecx;
				edx = 0; /* Not required */

				stress_asm_x86_cpuid(eax, ebx, ecx, edx);

				saved_regs[i].eax = eax;
				saved_regs[i].ebx = ebx;
				saved_regs[i].ecx = ecx;
				saved_regs[i].edx = edx;
			}
		}

		t = stress_time_now();
		for (j = 0; j < 1024; j++) {
PRAGMA_UNROLL_N(8)
			for (i = 0; i < n; i++) {
				uint32_t eax, ebx, ecx, edx;

				eax = reordered_cpu_regs[i].eax;
				ebx = 0; /* Not required */
				ecx = reordered_cpu_regs[i].ecx;
				edx = 0; /* Not required */

				stress_asm_x86_cpuid(eax, ebx, ecx, edx);
			}
			stress_bogo_inc(args);
		}
		duration += stress_time_now() - t;
		count += (double)n * (double)j;

		for (i = 0; i < n; i++) {
			if (stress_cpuid_regs[i].verify) {
				uint32_t eax, ebx, ecx, edx;

				eax = stress_cpuid_regs[i].eax;
				ebx = 0; /* Not required */
				ecx = stress_cpuid_regs[i].ecx;
				edx = 0; /* Not required */

				stress_asm_x86_cpuid(eax, ebx, ecx, edx);

				if (saved_regs[i].eax != eax) {
					pr_fail("%s: cpuid eax=0x%8.8" PRIx32 ", ecx=0x%8.8" PRIx32
						", got eax=0x%8.8" PRIx32 ", expecting 0x%8.8" PRIx32 "\n",
						args->name,
						stress_cpuid_regs[i].eax, stress_cpuid_regs[i].ecx,
						eax, saved_regs[i].eax);
					rc = EXIT_FAILURE;
				}
				if (saved_regs[i].ebx != ebx) {
					pr_fail("%s: cpuid eax=0x%8.8" PRIx32 ", ecx=0x%8.8" PRIx32
						", got ebx=0x%8.8" PRIx32 ", expecting 0x%8.8" PRIx32 "\n",
						args->name,
						stress_cpuid_regs[i].eax, stress_cpuid_regs[i].ecx,
						ebx, saved_regs[i].ebx);
					rc = EXIT_FAILURE;
				}
				if (saved_regs[i].ecx != ecx) {
					pr_fail("%s: cpuid eax=0x%8.8" PRIx32 ", ecx=0x%8.8" PRIx32
						", got ecx=0x%8.8" PRIx32 ", expecting 0x%8.8" PRIx32 "\n",
						args->name,
						stress_cpuid_regs[i].eax, stress_cpuid_regs[i].ecx,
						ecx, saved_regs[i].ecx);
					rc = EXIT_FAILURE;
				}
				if (saved_regs[i].edx != edx) {
					pr_fail("%s: cpuid eax=0x%8.8" PRIx32 ", ecx=0x%8.8" PRIx32
						", got edx=0x%8.8" PRIx32 ", expecting 0x%8.8" PRIx32 "\n",
						args->name,
						stress_cpuid_regs[i].eax, stress_cpuid_regs[i].ecx,
						edx, saved_regs[i].edx);
					rc = EXIT_FAILURE;
				}
			}
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per cpuid instruction",
		STRESS_DBL_NANOSECOND * rate, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

const stressor_info_t stress_x86cpuid_info = {
	.stressor = stress_x86cpuid,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else

const stressor_info_t stress_x86cpuid_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without x86 cpuid instruction support"
};
#endif
