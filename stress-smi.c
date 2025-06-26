/*
 * Copyright (C) 2021 Canonical, Ltd.
 * Copyright (C) 2022 Colin Ian King.
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
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-capabilities.h"
#include "core-module.h"

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"smi N",	"start N workers that trigger SMIs" },
	{ NULL,	"smi-ops N",	"stop after N SMIs have been triggered" },
	{ NULL,	NULL,		NULL }
};

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_IOPORT) &&		\
    defined(HAVE_SYS_IO_H) &&		\
    defined(__linux__)

#define MSR_SMI_COUNT   (0x00000034)
#define APM_PORT	(0xb2)
#define STRESS_SMI_NOP	(0x90)	/* SMI No-op command */

#if defined(STRESS_ARCH_X86_64)
typedef struct {
	uint64_t regs[16];
} smi_regs_t;

/*
 *  Stringification macros
 */
#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

#define SAVE_REG(r, reg, idx)			\
	__asm__ __volatile__("mov %%" XSTRINGIFY(reg) ", %0\n" : "+m" (r.regs[idx]))

static const char * const reg_names[] = {
	"r8",	/* 0 */
	"r9",	/* 1 */
	"r10",	/* 2 */
	"r11",	/* 3 */
	"r12",	/* 4 */
	"r13",	/* 5 */
	"r14",	/* 6 */
	"r15",	/* 7 */
	"rsi",	/* 8 */
	"rdi",	/* 9 */
	"rbp",	/* 10 */
	"rax",	/* 11 */
	"rbx",	/* 12 */
	"rcx",	/* 13 */
	"rdx",	/* 14 */
	"rsp"	/* 15 */
};

#define SAVE_REGS(r)			\
do {					\
	SAVE_REG(r, r8, 0);		\
	SAVE_REG(r, r9, 1);		\
	SAVE_REG(r, r10, 2);		\
	SAVE_REG(r, r11, 3);		\
	SAVE_REG(r, r12, 4);		\
	SAVE_REG(r, r13, 5);		\
	SAVE_REG(r, r14, 6);		\
	SAVE_REG(r, r15, 7);		\
	SAVE_REG(r, rsi, 8);		\
	SAVE_REG(r, rdi, 9);		\
	SAVE_REG(r, rbp, 10);		\
	SAVE_REG(r, rax, 11);		\
	SAVE_REG(r, rbx, 12);		\
	SAVE_REG(r, rcx, 13);		\
	SAVE_REG(r, rdx, 14);		\
	SAVE_REG(r, rsp, 15);		\
} while (0)

#endif

/*
 *  stress_smi_supported()
 *      check if we can run this with SHIM_CAP_SYS_MODULE capability
 */
static int stress_smi_supported(const char *name)
{
        if (!stress_check_capability(SHIM_CAP_SYS_MODULE)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with CAP_SYS_MODULE "
                        "rights for this stressor\n", name);
                return -1;
        }
        if (!stress_check_capability(SHIM_CAP_SYS_RAWIO)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with CAP_SYS_RAWIO "
                        "rights for this stressor\n", name);
                return -1;
        }
	if (!stress_check_capability(SHIM_CAP_IS_ROOT)) {
                pr_inf_skip("%s stressor will be skipped, "
                        "need to be running with root"
                        "rights for this stressor\n", name);
                return -1;
	}
	if (!stress_cpu_x86_has_msr()) {
                pr_inf_skip("%s stressor will be skipped, "
                        "CPU cannot read model specific registers (MSR)\n",
                        name);
	}
        return 0;
}

/*
 *  stress_smi_count()
 *	read SMI count across all CPUs, return -1 if not readable
 */
static int stress_smi_count(const int cpus, uint64_t *count)
{
	register int i;

	*count = 0;

	for (i = 0; i < cpus; i++) {
		uint64_t val;
		int ret;

		ret = stress_x86_readmsr64(i, MSR_SMI_COUNT, &val);
		if (UNLIKELY(ret < 0))
			return -1;
		*count += val;
	}
	return 0;
}

/*
 *  stress_smi()
 *	stress x86 systems by triggering SMIs
 */
static int stress_smi(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	bool already_loaded = false;
	bool read_msr_ok = true;
	bool load_module = false;
	uint64_t s1 = 0, val;
	double d1 = 0.0;
	const int cpus = stress_get_processors_online();
#if defined(STRESS_ARCH_X86_64)
	static smi_regs_t r1, r2;
#endif

	/*
	 *  If MSR can't be read maybe we need to load
	 *  the module to do so
	 */
	if (stress_x86_readmsr64(0, MSR_SMI_COUNT, &val) < 0)
		load_module = true;

	/*
	 *  Module load failure is not a problem, it just means
	 *  we can't get the SMI count
	 */
	if (load_module && (stress_instance_zero(args))) {
		VOID_RET(int, stress_module_load(args->name, "msr", NULL, &already_loaded));
	}

	if (ioperm(APM_PORT, 2, 1) < 0) {
		pr_inf_skip("%s: stressor will be skipped, cannot enable write "
			"permissions on the APM port 0x%2x\n",
			args->name, APM_PORT);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_instance_zero(args)) {
		d1 = stress_time_now();
		if (stress_smi_count(cpus, &s1) < 0)
			read_msr_ok = false;
	}

#if defined(STRESS_ARCH_X86_64)
	(void)shim_memset(&r1, 0, sizeof(r1));
	(void)shim_memset(&r2, 0, sizeof(r2));
#endif

	do {
#if defined(STRESS_ARCH_X86_64)
		size_t i;
#endif
		const uint16_t port = APM_PORT;
		const uint8_t data = STRESS_SMI_NOP;

#if defined(STRESS_ARCH_X86_64)
		SAVE_REGS(r1);
#endif
		__asm__ __volatile__(
			"out %0,%1\n\t" :: "a" (data), "d" (port));
#if defined(STRESS_ARCH_X86_64)
		SAVE_REGS(r2);
		/* out instruction clobbers rax, rdx, so copy these */
		r2.regs[11] = r1.regs[11];	/* RAX */
		r2.regs[14] = r1.regs[14];	/* RDX */

		/* check for register clobbering */
		for (i = 0; i < SIZEOF_ARRAY(r1.regs); i++) {
			if (UNLIKELY(r1.regs[i] != r2.regs[i])) {
				pr_fail("%s: register %s, before SMI: %" PRIx64 ", after SMI: %" PRIx64 "\n",
					args->name, reg_names[i],
					r1.regs[i], r2.regs[i]);
				rc = EXIT_FAILURE;
			}
		}
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	VOID_RET(int, ioperm(APM_PORT, 2, 0));

	if (stress_instance_zero(args)) {
		uint64_t s2;
		const double d2 = stress_time_now();

		if (stress_smi_count(cpus, &s2) < 0)
			read_msr_ok = false;

		if (read_msr_ok) {
			const double secs = d2 - d1;
			const uint64_t smis = (s2 - s1) / (uint64_t)cpus;
			const double rate = (secs > 0.0) ? (double)smis / secs : 0.0;
			const double duration = (rate > 0.0) ? STRESS_DBL_MICROSECOND / rate : 0.0;

			if ((secs > 0.0) && (duration > 0.0)) {
				pr_inf("%s: %.2f SMIs per second per CPU (%.2f microsecs per SMI)\n",
					args->name, rate, duration);
			} else {
				pr_inf("%s: cannot determine SMI rate, data is not unreliable\n",
					args->name);
			}
		} else {
			pr_inf("%s: cannot determine SMI rate, MSR_SMI_COUNT not readable\n",
				args->name);
		}

		if (load_module) {
			VOID_RET(int, stress_module_unload(args->name, "msr", already_loaded));
		}
	}

	return rc;
}

const stressor_info_t stress_smi_info = {
	.stressor = stress_smi,
	.classifier = CLASS_CPU | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.supported = stress_smi_supported
};
#else
const stressor_info_t stress_smi_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built for non-x86 target without sys/io.h or ioperm() or out op-code"
};
#endif
