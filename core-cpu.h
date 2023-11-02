/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#ifndef CORE_CPU_H
#define CORE_CPU_H

#include "core-version.h"
#include "core-arch.h"

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
extern WARN_UNUSED bool stress_cpu_x86_has_serialize(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx_vnni(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_vl(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_vnni(void);
extern WARN_UNUSED bool stress_cpu_x86_has_avx512_bw(void);

#endif
