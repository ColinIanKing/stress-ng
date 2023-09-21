// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-asm-arm.h"
#include "core-asm-ppc64.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu.h"

static const stress_help_t help[] = {
	{ NULL,	"waitcpu N",		"start N workers exercising wait/pause/nop instructions" },
	{ NULL,	"waitcpu-ops N",	"stop after N wait/pause/nop bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef void (*waitfunc_t)(void);
typedef bool (*waitfunc_supported_t)(void);

typedef struct {
	const char *name;
	const waitfunc_t waitfunc;
	const waitfunc_supported_t waitfunc_supported;
	uint8_t	supported;
	double count;
	double duration;
	double rate;
} stress_waitcpu_method_t;

static bool stress_waitcpu_nop_supported(void)
{
#if defined(HAVE_ASM_NOP)
	return true;
#else
	return false;
#endif
}

static void stress_waitcpu_nop(void)
{
	stress_asm_nop();
}

#if defined(HAVE_ASM_ARM_YIELD)
static inline bool stress_waitcpu_arm_yield_supported(void)
{
	return true;
}

static inline void stress_waitcpu_arm_yield(void)
{
	stress_asm_arm_yield();
}
#endif

#if defined(STRESS_ARCH_X86)

#if defined(HAVE_ASM_X86_PAUSE)
static bool stress_waitcpu_x86_pause_supported(void)
{
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
	if (!stress_cpu_is_x86())
		return false;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);
	/* Pentium 4 or higher? */
	return (eax > 0x02);
}

static void stress_waitcpu_x86_pause(void)
{
	stress_asm_x86_pause();
}
#endif

#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
static bool stress_waitcpu_x86_tpause_supported(void)
{
	if (!stress_cpu_is_x86())
		return false;
	return stress_cpu_x86_has_waitpkg();
}

static void stress_waitcpu_x86_tpause0(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	tsc = stress_asm_x86_rdtsc();
	ret = stress_asm_x86_tpause(0, tsc + delay);
	delay += (ret == 0) ? delay >> 6 : -(delay >> 6);
}

static void stress_waitcpu_x86_tpause1(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	tsc = stress_asm_x86_rdtsc();
	ret = stress_asm_x86_tpause(1, tsc + delay);
	delay += (ret == 0) ? delay >> 6 : -(delay >> 6);
}
#endif

#if !defined(HAVE_COMPILER_PCC) &&	\
    defined(HAVE_ARCH_X86_64)
static bool stress_waitcpu_x86_umwait_supported(void)
{
	if (!stress_cpu_is_x86())
		return false;
	return stress_cpu_x86_has_waitpkg();
}

static void stress_waitcpu_x86_umwait0(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	stress_asm_x86_umonitor(&delay);	/* Use dummy variable */
	tsc = stress_asm_x86_rdtsc();
	ret = stress_asm_x86_umwait(0, tsc + delay);
	delay += (ret == 0) ? delay >> 6 : -(delay >> 6);
}

static void stress_waitcpu_x86_umwait1(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	stress_asm_x86_umonitor(&delay);	/* Use dummy variable */
	tsc = stress_asm_x86_rdtsc();
	ret = stress_asm_x86_umwait(0, tsc + delay);
	delay += (ret == 0) ? delay >> 6 : -(delay >> 6);
}
#endif

#endif

#if defined(STRESS_ARCH_PPC64)
static bool stress_waitcpu_ppc64_supported(void)
{
	return true;
}

static void stress_waitcpu_ppc64_yield(void)
{
	stress_asm_ppc64_yield();
}

static void stress_waitcpu_ppc64_mdoio(void)
{
	stress_asm_ppc64_mdoio();
}

static void stress_waitcpu_ppc64_mdoom(void)
{
	stress_asm_ppc64_mdoom();
}
#endif

stress_waitcpu_method_t stress_waitcpu_method[] = {
	{ "nop",	stress_waitcpu_nop,		stress_waitcpu_nop_supported,		false, 0.0, 0.0, 0.0 },
#if defined(STRESS_ARCH_X86)
#if defined(HAVE_ASM_X86_PAUSE)
	{ "pause",	stress_waitcpu_x86_pause,	stress_waitcpu_x86_pause_supported,	false, 0.0, 0.0, 0.0 },
#endif
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
	{ "tpause0",	stress_waitcpu_x86_tpause0,	stress_waitcpu_x86_tpause_supported,	false, 0.0, 0.0, 0.0 },
	{ "tpause1",	stress_waitcpu_x86_tpause1,	stress_waitcpu_x86_tpause_supported,	false, 0.0, 0.0, 0.0 },
#endif
#if !defined(HAVE_COMPILER_PCC) &&	\
    defined(HAVE_ARCH_X86_64)
	{ "umwait0",	stress_waitcpu_x86_umwait0,	stress_waitcpu_x86_umwait_supported,	false, 0.0, 0.0, 0.0 },
	{ "umwait0",	stress_waitcpu_x86_umwait1,	stress_waitcpu_x86_umwait_supported,	false, 0.0, 0.0, 0.0 },
#endif
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	{ "yield",	stress_waitcpu_arm_yield,	stress_waitcpu_arm_yield_supported,	false, 0.0, 0.0, 0.0 },
#endif
#if defined(STRESS_ARCH_PPC64)
	{ "mdoio",	stress_waitcpu_ppc64_mdoio,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0, 0.0 },
	{ "mdoom",	stress_waitcpu_ppc64_mdoom,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0, 0.0 },
	{ "yield",	stress_waitcpu_ppc64_yield,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0, 0.0 },
#endif
};

/*
 *  stress_waitcpu()
 *     spin loop with cpu waiting
 */
static int stress_waitcpu(const stress_args_t *args)
{
	bool supported = false;
	size_t i, j;
	char str[16 * SIZEOF_ARRAY(stress_waitcpu_method)];
#if defined(STRESS_ARCH_X86)
	double nop_rate = -1.0;
#endif
	int rc = EXIT_SUCCESS;

	(void)shim_memset(str, 0, sizeof(str));

	for (i = 0; i < SIZEOF_ARRAY(stress_waitcpu_method); i++) {
		stress_waitcpu_method[i].supported = stress_waitcpu_method[i].waitfunc_supported();
		if (stress_waitcpu_method[i].supported) {
			supported |= true;
			(void)shim_strlcat(str, " ", sizeof(str));
			(void)shim_strlcat(str, stress_waitcpu_method[i].name, sizeof(str));
		}
		stress_waitcpu_method[i].duration = 0.0;
		stress_waitcpu_method[i].count = 0.0;
	}
	if (!supported) {
		if (args->instance == 0)
			pr_inf("%s: no CPU wait/pause instructions available, skipping stressor\n",
				args->name);
		return EXIT_NO_RESOURCE;
	}
	if (args->instance == 0)
		pr_inf("%s: exercising instruction%s:%s\n", args->name,
			SIZEOF_ARRAY(stress_waitcpu_method) > 1 ? "s" : "",
			str);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	do {
		for (i = 0; (i < SIZEOF_ARRAY(stress_waitcpu_method)) && stress_continue(args); i++) {
			const int loops = 1000;

			if (stress_waitcpu_method[i].supported) {
				double t;
				register int l;

				t = stress_time_now();
				for (l = 0; l < loops; l++) {
					stress_waitcpu_method[i].waitfunc();
				}
				stress_waitcpu_method[i].duration += stress_time_now() - t;
				stress_waitcpu_method[i].count += (double)loops;
				stress_bogo_inc(args);
			}
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0, j = 0; i < SIZEOF_ARRAY(stress_waitcpu_method); i++) {
		double rate = 0.0;

		if ((stress_waitcpu_method[i].duration > 0.0) &&
		    (stress_waitcpu_method[i].count > 0.0))
			rate = stress_waitcpu_method[i].count /
			       stress_waitcpu_method[i].duration;;

#if defined(STRESS_ARCH_X86)
		if (!strcmp("nop", stress_waitcpu_method[i].name))
			nop_rate = rate;
#endif

		if (rate > 0.0) {
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s ops per sec", stress_waitcpu_method[i].name);
			stress_metrics_set(args, j, msg, rate);
			j++;
		}
		stress_waitcpu_method[i].rate = rate;
	}

#if defined(STRESS_ARCH_X86)
	if (nop_rate > 0.0) {
		for (i = 0; i < SIZEOF_ARRAY(stress_waitcpu_method); i++) {
			if (!strcmp("nop", stress_waitcpu_method[i].name))
				continue;
			/*
			 *   compare with ~50% slop
			 */
			if (stress_waitcpu_method[i].rate > (nop_rate * 1.50)) {
				pr_fail("%s: %s instruction rate (%.2f ops "
					"per sec) is higher than nop "
					"instruction rate (%.2f ops per sec)\n",
					args->name, stress_waitcpu_method[i].name,
					stress_waitcpu_method[i].rate, nop_rate);
				rc = EXIT_FAILURE;
			}
		}
	}
#endif

	return rc;
}

stressor_info_t stress_waitcpu_info = {
	.stressor = stress_waitcpu,
	.class = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
