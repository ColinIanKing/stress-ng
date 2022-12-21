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
#ifndef CORE_CPU_H
#define CORE_CPU_H

#include "stress-version.h"
#include "core-arch.h"

/* Always included after stress-ng.h is included */

#if defined(STRESS_ARCH_X86)
#if defined(STRESS_ARCH_X86_32) && !NEED_GNUC(5, 0, 0) && defined(__PIC__)
#define stress_x86_cpuid(a, b, c, d)			\
	do {						\
		__asm__ __volatile__ (			\
			"pushl %%ebx\n"			\
			"cpuid\n"			\
			"mov %%ebx,%1\n"		\
			"popl %%ebx\n"			\
			: "=a"(a),			\
			  "=r"(b),			\
			  "=r"(c),			\
			  "=d"(d)			\
			: "0"(a),"2"(c));		\
	} while (0)
#else
#define stress_x86_cpuid(a, b, c, d)			\
	do {						\
		__asm__ __volatile__ (			\
			"cpuid\n"			\
			: "=a"(a),			\
			  "=b"(b),			\
			  "=c"(c),			\
			  "=d"(d)			\
			: "0"(a),"2"(c));		\
	} while (0)
#endif
#else
#define stress_x86_cpuid(a, b, c, d)			\
	do {						\
		a = 0;					\
		b = 0;					\
		c = 0;					\
		d = 0;					\
	} while (0)
#endif

extern WARN_UNUSED bool stress_cpu_is_x86(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clflushopt(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clwb(void);
extern WARN_UNUSED bool stress_cpu_x86_has_cldemote(void);
extern WARN_UNUSED bool stress_cpu_x86_has_waitpkg(void);
extern WARN_UNUSED bool stress_cpu_x86_has_rdseed(void);
extern WARN_UNUSED bool stress_cpu_x86_has_syscall(void);
extern WARN_UNUSED bool stress_cpu_x86_has_rdrand(void);
extern WARN_UNUSED bool stress_cpu_x86_has_tsc(void);
extern WARN_UNUSED bool stress_cpu_x86_has_msr(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clfsh(void);
extern WARN_UNUSED bool stress_cpu_x86_has_mmx(void);
extern WARN_UNUSED bool stress_cpu_x86_has_sse(void);
extern WARN_UNUSED bool stress_cpu_x86_has_sse2(void);

#endif
