/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
						/* Input -> Output */
#define CPUID_tsc		(1U << 4)	/* EAX=0x1 -> EDX */
#define CPUID_msr		(1U << 5)	/* EAX=0x1 -> EDX */
#define CPUID_clfsh		(1U << 19)	/* EAX=0x1 -> EDX */
#define CPUID_rdrand		(1U << 30)	/* EAX=0x1 -> ECX */
#define CPUID_rdseed		(1U << 18)	/* EAX=0x7, ECX=0x0 -> EBX */
#define CPUID_pcommit		(1U << 22)	/* EAX=0x7, ECX=0x0, -> EBX */
#define CPUID_clflushopt	(1U << 23)	/* EAX=0x7, ECX=0x0, -> EBX */
#define CPUID_clwb		(1U << 24)	/* EAX=0x7, ECX=0x0, -> EBX */
#define CPUID_cldemote		(1U << 25)	/* EAX=0x7, ECX=00x, -> ECX */
#define CPUID_syscall		(1U << 11)	/* EAX=0x80000001  -> EDX */

/*
 *  stress_x86_cpuid()
 *	cpuid for x86
 */
void stress_x86_cpuid(
	uint32_t *eax,
	uint32_t *ebx,
	uint32_t *ecx,
	uint32_t *edx)
{
#if defined(STRESS_ARCH_X86)
        asm volatile("cpuid"
            : "=a" (*eax),
              "=b" (*ebx),
              "=c" (*ecx),
              "=d" (*edx)
            : "0" (*eax), "2" (*ecx)
            : "memory");
#else
	*eax = 0;
	*ebx = 0;
	*ecx = 0;
	*edx = 0;
#endif
}

/*
 *  stress_cpu_is_x86()
 *	Intel x86 test
 */
bool stress_cpu_is_x86(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

	/* Intel CPU? */
	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);
	if ((memcmp(&ebx, "Genu", 4) == 0) &&
	    (memcmp(&edx, "ineI", 4) == 0) &&
	    (memcmp(&ecx, "ntel", 4) == 0))
		return true;
	else
		return false;
#else
	return false;
#endif
}

#if defined(STRESS_ARCH_X86)
/*
 *  stress_cpu_x86_extended_features
 *	cpuid EAX=7, ECX=0: Extended Features
 */
static void stress_cpu_x86_extended_features(
	uint32_t *ebx,
	uint32_t *ecx,
	uint32_t *edx)
{
	uint32_t eax = 7;

	stress_x86_cpuid(&eax, ebx, ecx, edx);
}
#endif

bool stress_cpu_x86_has_clflushopt(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(&ebx, &ecx, &edx);

	return !!(ebx & CPUID_clflushopt);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_clwb(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(&ebx, &ecx, &edx);

	return !!(ebx & CPUID_clwb);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_cldemote(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(&ebx, &ecx, &edx);

	return !!(ecx & CPUID_cldemote);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_rdseed(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_cpu_x86_extended_features(&ebx, &ecx, &edx);

	return !!(ebx & CPUID_rdseed);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_syscall(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x80000001, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

	return !!(edx & CPUID_syscall);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_rdrand(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

	return !!(ecx & CPUID_rdrand);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_tsc(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

	return !!(edx & CPUID_tsc);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_msr(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

	return !!(edx & CPUID_msr);
#else
	return false;
#endif
}

bool stress_cpu_x86_has_clfsh(void)
{
#if defined(STRESS_ARCH_X86)
	uint32_t eax = 0x1, ebx = 0, ecx = 0, edx = 0;

	if (!stress_cpu_is_x86())
		return false;

	stress_x86_cpuid(&eax, &ebx, &ecx, &edx);

	return !!(edx & CPUID_clfsh);
#else
	return false;
#endif
}
