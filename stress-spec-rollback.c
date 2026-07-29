/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-asm-generic.h"
#include "core-asm-arm.h"
#include "core-asm-loong64.h"
#include "core-asm-openrisc.h"
#include "core-asm-ppc64.h"
#include "core-asm-sparc.h"
#include "core-asm-riscv.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-pragma.h"

#define MIN_ROLLBACK_SIZE	(16)
#define MAX_ROLLBACK_SIZE	(1024 * 1024 * 8)
#define DEFAULT_ROLLBACK_SIZE	(256)

static const stress_help_t help[] = {
	{ NULL,	"spec-rollback N",      "start N workers exercising speculation rollbacks" },
	{ NULL,	"spec-rollback-ops N",  "stop after N speculation rollback bogo operations" },
	{ NULL, "spec-rollback-size N", "number of items in speculation read buffer" },
	{ NULL,	NULL,                   NULL }
};

static inline void ALWAYS_INLINE stress_spec_rollback_membarrier(void)
{
#if defined(STRESS_ARCH_ARM) &&		\
    defined(HAVE_ASM_ARM_DMB_SY) &&	\
    defined(HAVE_ASM_ARM_ISB)
	stress_asm_arm_dmb_sy();
	stress_asm_arm_isb();
	stress_asm_mb();
#elif defined(STRESS_ARCH_LOONG64) &&	\
    defined(HAVE_ASM_LOONG64_DBAR)
	stress_asm_loong64_dbar();
	stress_asm_mb();
#elif defined(STRESS_ARCH_OR1K) &&	\
    defined(HAVE_ASM_OPENRISC_MSYNC)
	stress_asm_openrisc_msync();
	stress_asm_mb();
#elif defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_FENCE_I) &&	\
    defined(HAVE_ASM_RISCV_FENCE_RW)
	stress_asm_riscv_fence_i();
	stress_asm_riscv_fence_rw();
	stress_asm_mb();
#elif defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_MSYNC)
	stress_asm_ppc64_msync();
	stress_asm_mb();
#elif defined(STRESS_ARCH_SPARC) &&	\
    defined(HAVE_ASM_SPARC_MEMBAR)
	stress_asm_sparc_membar();
	stress_asm_mb();
#elif defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_LFENCE) &&	\
    defined(HAVE_ASM_X86_MFENCE)
	stress_asm_x86_lfence();
	stress_asm_x86_mfence();
	stress_asm_mb();
#else
	stress_asm_mb();
#endif
}

/*
 *  stress_spec_rollback
 *	stress speculative
 */
static int stress_spec_rollback(stress_args_t *args)
{
	volatile uint64_t *values;
	size_t spec_rollback_size = DEFAULT_ROLLBACK_SIZE;
	size_t spec_rollback_size_div2;
	size_t i;
	size_t mask = 1;
	size_t values_size;

	(void)stress_setting_get("spec-rollback-size", &spec_rollback_size);
	values_size = spec_rollback_size * sizeof(*values);
	spec_rollback_size_div2 = spec_rollback_size >> 1;
	mask = (1U << stress_log2(spec_rollback_size_div2)) - 1;

	values = (volatile uint64_t *)stress_mmap_populate(NULL, values_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (values == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes, errno=%d (%s), skipping stressor\n",
			args->name, values_size, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < spec_rollback_size; i++)
		values[i] = (uint64_t)i;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
PRAGMA_UNROLL
		for (i = 0; i < 1024; i++) {
			register size_t idx = stress_mwcsizemodn(spec_rollback_size);
			uint64_t val;

			if (values[idx] < spec_rollback_size_div2) {
				val = values[idx ^ 0xf];
				(void)val;
			} else {
				val = values[idx & mask];
				(void)val;
			}
			stress_spec_rollback_membarrier();
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)munmap(shim_unvolatile_ptr(values), values_size);

	return EXIT_SUCCESS;
}

static const stress_opt_t opts[] = {
	{ OPT_spec_rollback_size, "spec-rollback-size", TYPE_ID_SIZE_T, MIN_ROLLBACK_SIZE, MAX_ROLLBACK_SIZE, NULL },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("branch"),
	STRESS_EX_FEATURE("branch-miss"),
	STRESS_EX_FEATURE("speculation-mispredict"),
	STRESS_EX_FEATURE("user-time"),

	STRESS_EX_END,
};

const stressor_info_t stress_spec_rollback_info = {
	.stressor = stress_spec_rollback,
	.classifier = CLASS_CPU | CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};
