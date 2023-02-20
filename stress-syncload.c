/*
 * Copyright (C) 2021-2023 Colin Ian King
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
#include "core-asm-x86.h"
#include "core-arch.h"
#include "core-cpu-cache.h"
#include "core-put.h"
#include "core-target-clones.h"

#define STRESS_SYNCLOAD_MS_DEFAULT	(125)	/* 125 milliseconds */
#define STRESS_SYNCLOAD_MS_MIN		(1)	/* 1 millisecond */
#define STRESS_SYNCLOAD_MS_MAX		(10000)	/* 1 second */

typedef void(* stress_syncload_op_t)(void);

static bool stress_sysload_x86_has_rdrand;

static const stress_help_t help[] = {
	{ NULL,	"syncload N",		"start N workers that synchronize load spikes" },
	{ NULL, "syncload-msbusy M",	"maximum busy duration in milliseconds" },
	{ NULL, "syncload-mssleep M",	"maximum sleep duration in milliseconds" },
	{ NULL,	"syncload-ops N",	"stop after N syncload bogo operations" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_syncload_ms(const char *opt, const char *setting)
{
	uint64_t ms;

	ms = stress_get_uint64(opt);
	stress_check_range(setting, ms, STRESS_SYNCLOAD_MS_MIN, STRESS_SYNCLOAD_MS_MAX);
	return stress_set_setting(setting, TYPE_ID_UINT64, &ms);
}

static int stress_set_syncload_msbusy(const char *opt)
{
	return stress_set_syncload_ms(opt, "syncload-msbusy");
}

static int stress_set_syncload_mssleep(const char *opt)
{
	return stress_set_syncload_ms(opt, "syncload-mssleep");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_syncload_msbusy,	stress_set_syncload_msbusy },
	{ OPT_syncload_mssleep,	stress_set_syncload_mssleep },
	{ 0,			NULL },
};

static void stress_syncload_none(void)
{
	return;
}

static void stress_syncload_nop(void)
{
#if defined(HAVE_ASM_NOP)
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
	__asm__ __volatile__("nop;\n");
#endif
}

#if defined(HAVE_ASM_X86_PAUSE)
static void stress_syncload_pause(void)
{
	stress_asm_x86_pause();
}
#endif

#if defined(HAVE_ASM_ARM_YIELD)
static void stress_syncload_yield(void)
{
	__asm__ __volatile__("yield;\n");
}
#endif

#if !defined(__TINYC__) &&		\
    defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_X86_RDRAND)
static void stress_syncload_rdrand(void)
{
	if (stress_sysload_x86_has_rdrand) {
		(void)stress_asm_x86_rdrand();
	} else {
		stress_syncload_nop();
	}
}
#endif

static void stress_syncload_sched_yield(void)
{
	shim_sched_yield();
}

static void stress_syncload_mfence(void)
{
	shim_mfence();
}

static void stress_syncload_mb(void)
{
	shim_mb();
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
	int niceness;

	niceness = nice(0);
	(void)niceness;
}

static void stress_syncload_spinwrite(void)
{
	register int i = 1000;

	while (i--)
		stress_uint32_put((uint32_t)i);
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
	stress_syncload_sched_yield,
#if !defined(__TINYC__) &&		\
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
#if defined(HAVE_ATOMIC_ADD_FETCH) &&	\
    defined(__ATOMIC_ACQUIRE)
	stress_syncload_atomic,
#endif
};

static inline double stress_syncload_gettime(void)
{
	return g_shared->syncload.start_time;
}

static void stress_syncload_init(void)
{
	g_shared->syncload.start_time = stress_time_now();
}

/*
 *  stress_syncload()
 *	stress that does lots of not a lot
 */
static int stress_syncload(const stress_args_t *args)
{
	uint64_t syncload_msbusy = STRESS_SYNCLOAD_MS_DEFAULT;
	uint64_t syncload_mssleep = STRESS_SYNCLOAD_MS_DEFAULT / 2;
	double timeout, sec_busy, sec_sleep;
	size_t delay_type = 0;

	(void)stress_get_setting("syncload-msbusy", &syncload_msbusy);
	(void)stress_get_setting("syncload-mssleep", &syncload_mssleep);

	sec_busy = (double)syncload_msbusy / STRESS_DBL_MILLISECOND;
	sec_sleep = (double)syncload_mssleep / STRESS_DBL_MILLISECOND;

	stress_sysload_x86_has_rdrand = stress_cpu_x86_has_rdrand();

	timeout = stress_syncload_gettime();

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double now;
		const stress_syncload_op_t op = stress_syncload_ops[delay_type];

		delay_type++;
		if (delay_type >= SIZEOF_ARRAY(stress_syncload_ops))
			delay_type = 0;

		timeout += sec_busy;
		while (keep_stressing_flag() && (stress_time_now() < timeout))
			op();

		timeout += sec_sleep;
		now = stress_time_now();
		if (now < timeout) {
			const uint64_t duration_us = (uint64_t)((timeout - now) * 1000000);

			shim_nanosleep_uint64(duration_us * 1000);
		}

		inc_counter(args);
	} while (keep_stressing(args));
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_syncload_info = {
	.stressor = stress_syncload,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.init = stress_syncload_init,
	.help = help
};
