/*
 * Copyright (C) 2025      Colin Ian King
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
#include "core-arch.h"

const char *stress_get_arch(void)
{
#if defined(STRESS_ARCH_ALPHA)
	return "Alpha";
#elif defined(STRESS_ARCH_ARC64)
	return "ARC";
#elif defined(STRESS_ARCH_ARM)
	return "ARM";
#elif defined(STRESS_ARCH_HPPA)
	return "HPPA";
#elif defined(STRESS_ARCH_KVX)
	return "KVX";
#elif defined(STRESS_ARCH_LOONG64)
	return "Loong64";
#elif defined(STRESS_ARCH_M68K)
	return "M68K";
#elif defined(STRESS_ARCH_MIPS)
	return "MIPS";
#elif defined(STRESS_ARCH_PPC)
	return "PPC";
#elif defined(STRESS_ARCH_PPC64)
	return "PPC64";
#elif defined(STRESS_ARCH_RISCV)
	return "RISC-V";
#elif defined(STRESS_ARCH_S390)
	return "S390X";
#elif defined(STRESS_ARCH_SH4)
	return "SH";
#elif defined(STRESS_ARCH_SPARC)
	return "SPARC";
#elif defined(STRESS_ARCH_X86_64)
	return "x86-64";
#elif defined(STRESS_ARCH_X86_32)
	return "x86-32";
#else
	return "unknown";
#endif
}
