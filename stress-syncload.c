/*
 * Copyright (C) 2021-2025 Colin Ian King
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
 */
#include "stress-ng.h"
#include "core-asm-arm.h"
#include "core-asm-x86.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-target-clones.h"

#include <math.h>

#define STRESS_SYNCLOAD_MS_DEFAULT	(125)	/* 125 milliseconds */
#define STRESS_SYNCLOAD_MS_MIN		(1)	/* 1 millisecond */
#define STRESS_SYNCLOAD_MS_MAX		(10000)	/* 1 second */

#if defined(__APPLE__)
#define REGISTER_PREFIX "r"
#else
#define REGISTER_PREFIX ""
#endif

#define REGISTER(r) REGISTER_PREFIX #r

typedef void(* stress_syncload_op_t)(void);

static bool stress_sysload_x86_has_rdrand;

/* Don't make these static otherwise O3 will optimize out the stores */
double fma_a[8];
double sqrt_r[4];

static const stress_help_t help[] = {
	{ NULL,	"syncload N",		"start N workers that synchronize load spikes" },
	{ NULL, "syncload-msbusy M",	"maximum busy duration in milliseconds" },
	{ NULL, "syncload-mssleep M",	"maximum sleep duration in milliseconds" },
	{ NULL,	"syncload-ops N",	"stop after N syncload bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_syncload_msbusy,	"syncload-msbusy",  TYPE_ID_UINT64, STRESS_SYNCLOAD_MS_MIN, STRESS_SYNCLOAD_MS_MAX, NULL },
	{ OPT_syncload_mssleep,	"syncload-mssleep", TYPE_ID_UINT64, STRESS_SYNCLOAD_MS_MIN, STRESS_SYNCLOAD_MS_MAX, NULL },
	END_OPT,
};

static void stress_syncload_none(void)
{
	return;
}

static void stress_syncload_nop(void)
{
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
	stress_asm_nop();
}

static void OPTIMIZE3 TARGET_CLONES stress_syncload_fma(void)
{
	const double scale = 2.32830643653870e-10;  /* 1.0 / 2^32 */
	double b = stress_mwc32() * scale;
	double c = stress_mwc32() * scale;

	fma_a[0] = (fma_a[0]) * b + c;
	fma_a[1] = (fma_a[1]) * b + c;
	fma_a[2] = (fma_a[2]) * b + c;
	fma_a[3] = (fma_a[3]) * b + c;
	fma_a[4] = (fma_a[4]) * b + c;
	fma_a[5] = (fma_a[5]) * b + c;
	fma_a[6] = (fma_a[6]) * b + c;
	fma_a[7] = (fma_a[7]) * b + c;
}

#if defined(HAVE_ASM_X86_PAUSE)
static void stress_syncload_pause(void)
{
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
	stress_asm_x86_pause();
}
#endif

#if defined(HAVE_ASM_ARM_YIELD)
static void stress_syncload_yield(void)
{
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
	stress_asm_arm_yield();
}
#endif

#if defined(STRESS_ARCH_PPC64) ||	\
    defined(STRESS_ARCH_PPC)
static void stress_syncload_yield(void)
{
        __asm__ __volatile__("or " REGISTER(27) "," REGISTER(27) "," REGISTER(27) ";\n");
        __asm__ __volatile__("or " REGISTER(27) "," REGISTER(27) "," REGISTER(27) ";\n");
        __asm__ __volatile__("or " REGISTER(27) "," REGISTER(27) "," REGISTER(27) ";\n");
        __asm__ __volatile__("or " REGISTER(27) "," REGISTER(27) "," REGISTER(27) ";\n");
}
#endif

#if !defined(HAVE_COMPILER_TCC) &&	\
    defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_X86_RDRAND)
static void stress_syncload_rdrand(void)
{
	if (stress_sysload_x86_has_rdrand) {
		(void)stress_asm_x86_rdrand();
		(void)stress_asm_x86_rdrand();
		(void)stress_asm_x86_rdrand();
		(void)stress_asm_x86_rdrand();
	} else {
		stress_syncload_nop();
	}
}
#endif

static void stress_syncload_sched_yield(void)
{
	(void)shim_sched_yield();
	(void)shim_sched_yield();
	(void)shim_sched_yield();
	(void)shim_sched_yield();
}

static void stress_syncload_mfence(void)
{
	shim_mfence();
	shim_mfence();
	shim_mfence();
	shim_mfence();
}

static void stress_syncload_mb(void)
{
	stress_asm_mb();
	stress_asm_mb();
	stress_asm_mb();
	stress_asm_mb();
}

static void stress_syncload_loop(void)
{
	register int i = 1000;

	while (i--) {
		__asm__ __volatile__("");
	}
}

#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_ACQUIRE)
static void stress_syncload_atomic(void)
{
	__atomic_add_fetch(&g_shared->syncload.value, 1, __ATOMIC_ACQUIRE);
}
#endif

static void stress_syncload_nice(void)
{
	VOID_RET(int, shim_nice(0));
}

static void stress_syncload_spinwrite(void)
{
	register int i = 1000;

	while (i--)
		stress_uint32_put((uint32_t)i);
}

static void OPTIMIZE_FAST_MATH TARGET_CLONES stress_syncload_sqrt(void)
{
	static double val = 0.0;
	size_t i;

PRAGMA_UNROLL_N(SIZEOF_ARRAY(sqrt_r))
	for (i = 0; i < SIZEOF_ARRAY(sqrt_r); i++) {
		sqrt_r[i] = shim_sqrt(val);
		val += 0.005;
	}
}

#if defined(HAVE_VECMATH)

typedef int8_t stress_vint8w1024_t       __attribute__ ((vector_size(1024 / 8)));

static void stress_syncload_vecmath_init(stress_vint8w1024_t *a, const size_t len)
{
	uint32_t *data = (uint32_t *)a;
	size_t i, n = len >> 2;

	for (i = 0; i < n; i++)
		data[i] = stress_mwc32();
}

static void TARGET_CLONES stress_syncload_vecmath(void)
{
	static stress_vint8w1024_t a, b, c;
	static bool init = false;

	if (!init) {
		stress_syncload_vecmath_init(&a, sizeof(a));
		stress_syncload_vecmath_init(&b, sizeof(b));
		stress_syncload_vecmath_init(&c, sizeof(c));
		init = true;
	}

	c *= b;
	c += a;
}

#endif

static const stress_syncload_op_t stress_syncload_ops[] = {
	stress_syncload_none,
	stress_syncload_nop,
#if defined(HAVE_ASM_X86_PAUSE)
	stress_syncload_pause,
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	stress_syncload_yield,
#endif
#if defined(STRESS_ARCH_PPC64) ||	\
    defined(STRESS_ARCH_PPC)
	stress_syncload_yield,
#endif
	stress_syncload_sched_yield,
#if !defined(HAVE_COMPILER_TCC) &&	\
    defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_X86_RDRAND)
	stress_syncload_rdrand,
#endif
	stress_syncload_mfence,
	stress_syncload_mb,
	stress_syncload_loop,
#if defined(HAVE_VECMATH)
	stress_syncload_vecmath,
#endif
	stress_syncload_nice,
	stress_syncload_spinwrite,
	stress_syncload_sqrt,
#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_ACQUIRE)
	stress_syncload_atomic,
#endif
	stress_syncload_fma,
};

static inline double stress_syncload_gettime(void)
{
	return g_shared->syncload.start_time;
}

static void stress_syncload_init(const uint32_t instances)
{
	(void)instances;

	g_shared->syncload.start_time = stress_time_now();
}

/*
 *  stress_syncload()
 *	stress that does lots of not a lot
 */
static int stress_syncload(stress_args_t *args)
{
	uint64_t syncload_msbusy = STRESS_SYNCLOAD_MS_DEFAULT;
	uint64_t syncload_mssleep = STRESS_SYNCLOAD_MS_DEFAULT / 2;
	double timeout, sec_busy, sec_sleep;
	size_t delay_type = 0;
	size_t i;

	stress_catch_sigill();

	(void)stress_get_setting("syncload-msbusy", &syncload_msbusy);
	(void)stress_get_setting("syncload-mssleep", &syncload_mssleep);

	sec_busy = (double)syncload_msbusy / STRESS_DBL_MILLISECOND;
	sec_sleep = (double)syncload_mssleep / STRESS_DBL_MILLISECOND;

	stress_sysload_x86_has_rdrand = stress_cpu_x86_has_rdrand();

	timeout = stress_syncload_gettime();

	for (i = 0; i < SIZEOF_ARRAY(fma_a); i++)
		fma_a[i] = 0.0;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double now;
		const stress_syncload_op_t op = stress_syncload_ops[delay_type];

		delay_type++;
		if (UNLIKELY(delay_type >= SIZEOF_ARRAY(stress_syncload_ops)))
			delay_type = 0;

		timeout += sec_busy;
		while (stress_continue_flag() && (stress_time_now() < timeout))
			op();

		timeout += sec_sleep;
		now = stress_time_now();
		if (now < timeout) {
			const uint64_t duration_us = (uint64_t)((timeout - now) * 1000000);

			(void)shim_nanosleep_uint64(duration_us * 1000);
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_syncload_info = {
	.stressor = stress_syncload,
	.classifier = CLASS_CPU,
	.opts = opts,
	.init = stress_syncload_init,
	.help = help
};
