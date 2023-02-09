/*
 * Copyright (C) 2023      Colin Ian King.
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
#include "core-asm-ppc64.h"
#include "core-asm-x86.h"
#include "core-cpu.h"

static const stress_help_t help[] = {
	{ NULL,	"waitcpu N",		"start N workers exercising wait/pause/nop instructions" },
	{ NULL,	"waitcpu-ops N",	"stop after N wait/pause/nop bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	const char *name;
	void (*waitfunc)(void);
	bool (*waitfunc_supported)(void);
	uint8_t	supported;
	double count;
	double duration;
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
#if defined(HAVE_ASM_NOP)

#if defined(STRESS_ARCH_KVX)
	/*
	 * Extra ;; required for KVX to indicate end of
	 * a VLIW instruction bundle
	 */
	__asm__ __volatile__("nop\n;;\n");
#else
	__asm__ __volatile__("nop;\n");
#endif
#endif
}

#if defined(HAVE_ASM_ARM_YIELD)
static inline bool stress_waitcpu_arm_yield_supported(void)
{
	return true;
}

static inline void stress_waitcpu_arm_yield(void)
{
	__asm__ __volatile__("yield;\n");
}
#endif

#if defined(STRESS_ARCH_X86)
static bool stress_waitcpu_x86_pause_supported(void)
{
#if defined(HAVE_ASM_X86_PAUSE)
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
	if (!stress_cpu_is_x86())
		return false;
	stress_asm_x86_cpuid(eax, ebx, ecx, edx);
	/* Pentium 4 or higher? */
	return (eax > 0x02);
#else
	return false;
#endif
}

static void stress_waitcpu_x86_pause(void)
{
	stress_asm_x86_pause();
}

static bool stress_waitcpu_x86_waitpkg_supported(void)
{
	if (!stress_cpu_is_x86())
		return false;
	return stress_cpu_x86_has_waitpkg();
}

#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(__PCC__)
static bool stress_waitcpu_x86_tpause_supported(void)
{
	return stress_waitcpu_x86_waitpkg_supported();
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

#if !defined(__PCC__)
static bool stress_waitcpu_x86_umwait_supported(void)
{
	return stress_waitcpu_x86_waitpkg_supported();
}

static void stress_waitcpu_x86_umwait0(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	stress_asm_x86_umonitor(&delay);
	tsc = stress_asm_x86_rdtsc();
	ret = stress_asm_x86_umwait(0, tsc + delay);
	delay += (ret == 0) ? delay >> 6 : -(delay >> 6);
}

static void stress_waitcpu_x86_umwait1(void)
{
	static uint64_t delay = 2048;
	register uint64_t tsc;
	register int ret;

	stress_asm_x86_umonitor(&delay);
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
	{ "nop",	stress_waitcpu_nop,		stress_waitcpu_nop_supported,		false, 0.0, 0.0 },
#if defined(STRESS_ARCH_X86)
	{ "pause",	stress_waitcpu_x86_pause,	stress_waitcpu_x86_pause_supported,	false, 0.0, 0.0 },
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(__PCC__)
	{ "tpause0",	stress_waitcpu_x86_tpause0,	stress_waitcpu_x86_tpause_supported,	false, 0.0, 0.0 },
	{ "tpause1",	stress_waitcpu_x86_tpause1,	stress_waitcpu_x86_tpause_supported,	false, 0.0, 0.0 },
#endif
#if !defined(__PCC__)
	{ "umwait0",	stress_waitcpu_x86_umwait0,	stress_waitcpu_x86_umwait_supported,	false, 0.0, 0.0 },
	{ "umwait0",	stress_waitcpu_x86_umwait1,	stress_waitcpu_x86_umwait_supported,	false, 0.0, 0.0 },
#endif
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	{ "yield",	stress_waitcpu_arm_yield,	stress_waitcpu_arm_yield_supported,	false, 0.0, 0.0 },
#endif
#if defined(STRESS_ARCH_PPC64)
	{ "mdoio",	stress_waitcpu_ppc64_mdoio,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0 },
	{ "mdoom",	stress_waitcpu_ppc64_mdoom,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0 },
	{ "yield",	stress_waitcpu_ppc64_yield,	stress_waitcpu_ppc64_supported,		false, 0.0, 0.0 },
#endif
};

/*
 *  stress_waitcpu()
 *     spin loop with cpu waiting
 */
static int stress_waitcpu(const stress_args_t *args)
{
	bool supported = false;
	size_t i;
	char str[16 * SIZEOF_ARRAY(stress_waitcpu_method)];

	(void)memset(str, 0, sizeof(str));

	for (i = 0; i < SIZEOF_ARRAY(stress_waitcpu_method); i++) {
		stress_waitcpu_method[i].supported = stress_waitcpu_method[i].waitfunc_supported();
		if (stress_waitcpu_method[i].supported) {
			supported |= true;
			shim_strlcat(str, " ", sizeof(str));
			shim_strlcat(str, stress_waitcpu_method[i].name, sizeof(str));
		}
		stress_waitcpu_method[i].duration = 0.0;
		stress_waitcpu_method[i].count = 0.0;
	}
	if (!supported) {
		if (args->instance == 0) {
			pr_inf("%s: no CPU wait/pause instructions available, skipping stressor\n",
				args->name);
		}
		return EXIT_NO_RESOURCE;
	}
	if (args->instance == 0) {
		pr_inf("%s: exercising:%s\n", args->name, str);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	do {
		for (i = 0; (i < SIZEOF_ARRAY(stress_waitcpu_method)) && keep_stressing(args); i++) {
			const int loops = 1000;
			double t;
			register int j;

			if (stress_waitcpu_method[i].supported) {
				t = stress_time_now();
				for (j = 0; j < loops; j++) {
					stress_waitcpu_method[i].waitfunc();
				}
				stress_waitcpu_method[i].duration += stress_time_now() - t;
				stress_waitcpu_method[i].count += (double)loops;
				inc_counter(args);
			}
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < SIZEOF_ARRAY(stress_waitcpu_method); i++) {
		if ((stress_waitcpu_method[i].duration > 0.0) &&
		    (stress_waitcpu_method[i].count > 0.0)) {
			char msg[64];
			const double rate = stress_waitcpu_method[i].count / stress_waitcpu_method[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s ops per sec", stress_waitcpu_method[i].name);
			stress_metrics_set(args, i, msg, rate);
		}
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_waitcpu_info = {
	.stressor = stress_waitcpu,
	.class = CLASS_CPU,
	.verify = VERIFY_NONE,
	.help = help
};
