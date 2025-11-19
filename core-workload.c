/*
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-workload.h"

#include "core-asm-arm.h"
#include "core-asm-loong64.h"
#include "core-asm-ppc64.h"
#include "core-asm-riscv.h"
#include "core-asm-x86.h"
#include "core-asm-generic.h"
#include "core-cpu-cache.h"
#include "core-builtin.h"
#include "core-put.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

#include <math.h>
#include <time.h>

const stress_workload_method_t workload_methods[] = {
	{ "all",	STRESS_WORKLOAD_METHOD_ALL },
	{ "fma",	STRESS_WORKLOAD_METHOD_FMA },
	{ "getpid",	STRESS_WORKLOAD_METHOD_GETPID },
	{ "time",	STRESS_WORKLOAD_METHOD_TIME },
	{ "inc64",	STRESS_WORKLOAD_METHOD_INC64 },
	{ "memmove",	STRESS_WORKLOAD_METHOD_MEMMOVE },
	{ "memread",	STRESS_WORKLOAD_METHOD_MEMREAD },
	{ "memset",	STRESS_WORKLOAD_METHOD_MEMSET },
	{ "mwc64",	STRESS_WORKLOAD_METHOD_MWC64 },
	{ "nop",	STRESS_WORKLOAD_METHOD_NOP },
	{ "pause",	STRESS_WORKLOAD_METHOD_PAUSE },
	{ "procname",	STRESS_WORKLOAD_METHOD_PROCNAME },
	{ "random",	STRESS_WORKLOAD_METHOD_RANDOM },
	{ "sqrt",	STRESS_WORKLOAD_METHOD_SQRT },
	{ "vecfp",	STRESS_WORKLOAD_METHOD_VECFP },
};

const char *stress_workload_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(workload_methods)) ? workload_methods[i].name : NULL;
}

static TARGET_CLONES void stress_workload_fma(void)
{
	const uint32_t r = stress_mwc32();
	const double a = (double)r;
	const double b = (double)(r >> 4);
	const double c = (double)(r ^ 0xa5a55a5a);

	stress_double_put((a * b) + c);
	stress_double_put((a * c) + b);
	stress_double_put((b * c) + a);

	stress_double_put(a + (b * c));
	stress_double_put(a + (c * b));
	stress_double_put(b + (c * a));
}

static void stress_workload_nop(void)
{
	register int i;

	for (i = 0; i < 16; i++) {
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
		stress_asm_nop();
	}
}

static void stress_workload_pause(void)
{
#if defined(HAVE_ASM_X86_PAUSE)
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
#elif defined(HAVE_ASM_ARM_YIELD)
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
#elif defined(STRESS_ARCH_PPC64)
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
	stress_asm_ppc64_yield();
#elif defined(STRESS_ARCH_PPC)
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
	stress_asm_ppc_yield();
#elif defined(STRESS_ARCH_RISCV)
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
	stress_asm_riscv_pause();
#elif defined(HAVE_ASM_LOONG64_DBAR)
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
	stress_asm_loong64_dbar();
#else
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
	stress_asm_mb();
	stress_asm_nop();
#endif
}

static void stress_workload_procname(const char *name)
{
	char procname[64];

	(void)snprintf(procname, sizeof(procname),
		"%s-%" PRIx64 "%" PRIx64 "%" PRIx64,
		name, stress_mwc64(), stress_mwc64(), stress_mwc64());
	stress_set_proc_name(procname);
}

static void OPTIMIZE3 TARGET_CLONES stress_workload_read(void *buffer, const size_t buffer_len)
{
#if defined(HAVE_VECMATH)
	typedef int64_t stress_vint64_t __attribute__ ((vector_size (128)));

	register stress_vint64_t *ptr = (stress_vint64_t *)buffer;
	register stress_vint64_t *end = (stress_vint64_t *)(((uintptr_t)buffer) + buffer_len);

	while (ptr < end) {
		stress_vint64_t v;

		v = *(volatile stress_vint64_t *)&ptr[0];
		(void)v;
		ptr += 2;
	}

	stress_cpu_data_cache_flush(buffer, buffer_len);
#else
	register uint64_t *ptr = (uint64_t *)buffer;
	register uint64_t *end = (uint64_t *)(((uintptr_t)buffer) + buffer_len);

	stress_cpu_data_cache_flush(buffer, buffer_len);
	while (ptr < end) {
		(void)*(volatile uint64_t *)&ptr[0x00];
		(void)*(volatile uint64_t *)&ptr[0x01];
		(void)*(volatile uint64_t *)&ptr[0x02];
		(void)*(volatile uint64_t *)&ptr[0x03];
		(void)*(volatile uint64_t *)&ptr[0x04];
		(void)*(volatile uint64_t *)&ptr[0x05];
		(void)*(volatile uint64_t *)&ptr[0x06];
		(void)*(volatile uint64_t *)&ptr[0x07];
		(void)*(volatile uint64_t *)&ptr[0x08];
		(void)*(volatile uint64_t *)&ptr[0x09];
		(void)*(volatile uint64_t *)&ptr[0x0a];
		(void)*(volatile uint64_t *)&ptr[0x0b];
		(void)*(volatile uint64_t *)&ptr[0x0c];
		(void)*(volatile uint64_t *)&ptr[0x0d];
		(void)*(volatile uint64_t *)&ptr[0x0e];
		(void)*(volatile uint64_t *)&ptr[0x0f];
		ptr += 16;
	}
#endif
}

static void stress_workload_sqrt(const double v1, const double v2)
{
	double r;

	r = sqrt(v1) + hypot(v1, v1 + v2);
	r += sqrt(v2) + hypot(v2, v1 + v2);
	r += sqrt(v1 + v2);

	stress_double_put(r);
}

static void TARGET_CLONES stress_workload_vecfp(void)
{
#if defined(HAVE_VECMATH)
	/* Explicit vectorized version */
	typedef union {
		double v   ALIGNED(2048) __attribute__ ((vector_size(sizeof(double) * 64)));
		double f[64] ALIGNED(2048);
	} stress_vecfp_double_64_t;

	stress_vecfp_double_64_t a, b;
	double sum = 0.0;
	static int v = 0;
	register size_t i;

	for (i = 0; i < 64; i++) {
		a.f[i] = v;
		b.f[i] = v * v;
		v++;
	}
	a.v *= b.v;
	a.v += -b.v;

	for (i = 0; i < 64; i++) {
		sum += a.f[i];
	}
	stress_double_put(sum);
#else
	/* See how well compiler can vectorize version */
        double a[64], b[64];
	double sum = 0.0;
	static int v = 0;
	register size_t i;

	for (i = 0; i < 64; i++) {
		a[i] = v;
		b[i] = v * v;
		v++;
	}
	for (i = 0; i < 64; i++) {
		a[i] *= b[i];
	}
	for (i = 0; i < 64; i++) {
		a[i] += b[i];
	}
	for (i = 0; i < 64; i++) {
		sum += a[i];
	}
	stress_long_double_put(sum);
#endif
}

void stress_workload_waste_time(
	const char *name,
	const int workload_method,
	const double run_duration_sec,
	uint8_t *buffer,
	const size_t buffer_len)
{
	const double t_end = stress_time_now() + run_duration_sec;
	double t;
	static volatile uint64_t val = 0;
	const int which = (workload_method == STRESS_WORKLOAD_METHOD_ALL) ?
		stress_mwc8modn(STRESS_WORKLOAD_METHOD_MAX) + 1 : workload_method;

	switch (which) {
	case STRESS_WORKLOAD_METHOD_TIME:
		while (stress_time_now() < t_end)
			(void)time(NULL);
		break;
	case STRESS_WORKLOAD_METHOD_NOP:
		while (stress_time_now() < t_end)
			stress_workload_nop();
		break;
	case STRESS_WORKLOAD_METHOD_MEMSET:
		while (stress_time_now() < t_end)
			shim_memset(buffer, stress_mwc8(), buffer_len);
		break;
	case STRESS_WORKLOAD_METHOD_MEMMOVE:
		while (stress_time_now() < t_end)
			shim_memmove(buffer, buffer + 1, buffer_len - 1);
		break;
	case STRESS_WORKLOAD_METHOD_SQRT:
		while ((t = stress_time_now()) < t_end)
			stress_workload_sqrt(t, t_end);
		break;
	case STRESS_WORKLOAD_METHOD_INC64:
		while (stress_time_now() < t_end)
			val++;
		break;
	case STRESS_WORKLOAD_METHOD_MWC64:
		while (stress_time_now() < t_end)
			(void)stress_mwc64();
		break;
	case STRESS_WORKLOAD_METHOD_GETPID:
		while (stress_time_now() < t_end)
			(void)getpid();
		break;
	case STRESS_WORKLOAD_METHOD_MEMREAD:
		while (stress_time_now() < t_end)
			stress_workload_read(buffer, buffer_len);
		break;
	case STRESS_WORKLOAD_METHOD_PAUSE:
		while (stress_time_now() < t_end)
			stress_workload_pause();
		break;
	case STRESS_WORKLOAD_METHOD_FMA:
		while (stress_time_now() < t_end)
			stress_workload_fma();
		break;
	case STRESS_WORKLOAD_METHOD_VECFP:
		while (stress_time_now() < t_end)
			stress_workload_vecfp();
		break;
	case STRESS_WORKLOAD_METHOD_PROCNAME:
		while (stress_time_now() < t_end)
			stress_workload_procname(name);
		break;
	case STRESS_WORKLOAD_METHOD_RANDOM:
	default:
		while ((t = stress_time_now()) < t_end) {
			switch (stress_mwc8modn(STRESS_WORKLOAD_METHOD_MAX) + 1) {
			case STRESS_WORKLOAD_METHOD_TIME:
				(void)time(NULL);
				break;
			case STRESS_WORKLOAD_METHOD_NOP:
				stress_workload_nop();
				break;
			case STRESS_WORKLOAD_METHOD_MEMSET:
				shim_memset(buffer, stress_mwc8(), buffer_len);
				break;
			case STRESS_WORKLOAD_METHOD_MEMMOVE:
				shim_memmove(buffer, buffer + 1, buffer_len - 1);
				break;
			case STRESS_WORKLOAD_METHOD_INC64:
				val++;
				break;
			case STRESS_WORKLOAD_METHOD_MWC64:
				(void)stress_mwc64();
				break;
			case STRESS_WORKLOAD_METHOD_GETPID:
				(void)getpid();
				break;
			case STRESS_WORKLOAD_METHOD_SQRT:
				stress_workload_sqrt(t, t_end);
				break;
			case STRESS_WORKLOAD_METHOD_MEMREAD:
				stress_workload_read(buffer, buffer_len);
				break;
			case STRESS_WORKLOAD_METHOD_PAUSE:
				stress_workload_pause();
				break;
			case STRESS_WORKLOAD_METHOD_FMA:
				stress_workload_fma();
				break;
			case STRESS_WORKLOAD_METHOD_VECFP:
				stress_workload_vecfp();
				break;
			default:
			case STRESS_WORKLOAD_METHOD_PROCNAME:
				stress_workload_procname(name);
				break;
			}
		}
		break;
	}
}
