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
#ifndef CORE_CPU_H
#define CORE_CPU_H

#include "core-version.h"
#include "core-arch.h"

extern WARN_UNUSED bool stress_cpu_is_x86(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx_vnni(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_vl(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_vnni(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_bw(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clflushopt(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clwb(void);
extern WARN_UNUSED bool stress_cpu_x86_has_cldemote(void);
extern WARN_UNUSED bool stress_cpu_x86_has_clfsh(void);
extern WARN_UNUSED bool stress_cpu_x86_has_lahf_lm(void);
extern WARN_UNUSED bool stress_cpu_x86_has_mmx(void);
extern WARN_UNUSED bool stress_cpu_x86_has_msr(void);
extern WARN_UNUSED bool stress_cpu_x86_has_prefetchwt1(void);
extern WARN_UNUSED bool stress_cpu_x86_has_rdrand(void);
extern WARN_UNUSED bool stress_cpu_x86_has_rdseed(void);
extern WARN_UNUSED bool stress_cpu_x86_has_rdtscp(void);
extern WARN_UNUSED bool stress_cpu_x86_has_serialize(void);
extern WARN_UNUSED bool stress_cpu_x86_has_sse(void);
extern WARN_UNUSED bool stress_cpu_x86_has_sse2(void);
extern WARN_UNUSED bool stress_cpu_x86_has_syscall(void);
extern WARN_UNUSED bool stress_cpu_x86_has_tsc(void);
extern WARN_UNUSED bool stress_cpu_x86_has_waitpkg(void);
extern WARN_UNUSED bool stress_cpu_x86_has_movdiri(void);

extern void stress_cpu_disable_fp_subnormals(void);
extern void stress_cpu_enable_fp_subnormals(void);

#endif
