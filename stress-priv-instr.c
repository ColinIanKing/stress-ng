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
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-put.h"

static const stress_help_t help[] = {
	{ NULL,	"priv-instr N",		"start N workers exercising privileged instruction" },
	{ NULL,	"priv-instr-ops N",	"stop after N bogo instruction operations" },
	{ NULL,	NULL,			NULL }
};

typedef void (*op_func_t)(void);

typedef struct {
	const char *instr;
	const op_func_t	op_func;
	bool invalid;
	bool trapped;
} op_info_t;

#if defined(STRESS_ARCH_ARM) &&		\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_ARM_TLBI)
#define HAVE_PRIV_INSTR
static void stress_arm_tlbi(void)
{
	__asm__ __volatile__("tlbi vmalle1is");
}

static op_info_t op_info[] =
{
	{ "tlbi",	stress_arm_tlbi,	false, false },
};
#endif

#if defined(STRESS_ARCH_ALPHA) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    (defined(HAVE_ASM_ALPHA_DRAINA) ||	\
     defined(HAVE_ASM_ALPHA_HALT))
#define HAVE_PRIV_INSTR

#define PAL_halt	0
#define PAL_draina	2

#if defined(HAVE_ASM_ALPHA_DRAINA)
static void stress_alpha_draina(void)
{
	__asm__ __volatile__("call_pal %0 #draina" : : "i" (PAL_draina) : "memory");
}
#endif

#if defined(HAVE_ASM_ALPHA_HALT)
static void stress_alpha_halt(void)
{
	__asm__ __volatile__("call_pal %0 #halt" : : "i" (PAL_halt));
}
#endif

static op_info_t op_info[] =
{
#if defined(HAVE_ASM_ALPHA_DRAINA)
	{ "call_pal %0 #draina",	stress_alpha_draina,	false, false },
#endif
#if defined(HAVE_ASM_ALPHA_HALT)
	{ "call_pal %0 #halt",		stress_alpha_halt,	false, false },
#endif
};
#endif

#if defined(STRESS_ARCH_HPPA) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    (defined(HAVE_ASM_HPPA_DIAG) || 	\
     defined(HAVE_ASM_HPPA_RFI))
#define HAVE_PRIV_INSTR

#if defined(HAVE_ASM_HPPA_DIAG)
static void stress_hppa_diag(void)
{
	__asm__ __volatile__("diag 0");
}
#endif

#if defined(HAVE_ASM_HPPA_RFI)
static void stress_hppa_rfi(void)
{
	__asm__ __volatile__("rfi");
}
#endif

static op_info_t op_info[] =
{
#if defined(HAVE_ASM_HPPA_DIAG)
	{ "diag",	stress_hppa_diag,	false, false },
#endif
#if defined(HAVE_ASM_HPPA_RFI)
	{ "rfi",	stress_hppa_rfi,	false, false },
#endif
};
#endif

#if defined(STRESS_ARCH_LOONG64) &&	\
    defined(HAVE_SIGLONGJMP)
#define HAVE_PRIV_INSTR


#if defined(HAVE_ASM_LOONG64_TLBRD)
static void stress_loong64_tlbrd(void)
{
	__asm__ __volatile__("tlbrd");
}
#endif

#if defined(HAVE_ASM_LOONG64_TLBSRCH)
static void stress_loong64_tlbsrch(void)
{
	__asm__ __volatile__("tlbsrch");
}
#endif

static op_info_t op_info[] =
{
#if defined(HAVE_ASM_LOONG64_TLBRD)
	{ "tlbrd",	stress_loong64_tlbrd,	false, false },
#endif
#if defined(HAVE_ASM_LOONG64_TLBSRCH)
	{ "tlbsrch",	stress_loong64_tlbsrch,	false, false },
#endif
};

#endif

#if defined(STRESS_ARCH_M68K) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_M68K_EORI_SR)
#define HAVE_PRIV_INSTR

static void stress_m68k_sr(void)
{
	__asm__ __volatile__("eori.w #0001,%sr");
}

static op_info_t op_info[] =
{
	{ "eori.w #1,sr",	stress_m68k_sr,	false, false },
};
#endif

#if defined(STRESS_ARCH_MIPS) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_MIPS_WAIT)
#define HAVE_PRIV_INSTR
static void stress_mips_wait(void)
{
	__asm__ __volatile__("wait");
}

static op_info_t op_info[] =
{
	{ "wait",	stress_mips_wait,	false, false },
};
#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_PPC64_TLBIE)
#define HAVE_PRIV_INSTR
#define HAVE_PRIV_PAGE

static void *page;

static void stress_ppc64_tlbie(void)
{
        unsigned long int address = (unsigned long int)page;

	__asm__ __volatile__("tlbie %0, 0" : : "r" (address) : "memory");
}

static op_info_t op_info[] =
{
	{ "tlbie",	stress_ppc64_tlbie,	false, false },
};
#endif

#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_RISCV_SFENCE_VMA)
#define HAVE_PRIV_INSTR
static void stress_riscv_sfence_vma(void)
{
	__asm__ __volatile__("sfence.vma" : : : "memory");
}

static op_info_t op_info[] =
{
	{ "sfence.vma",	stress_riscv_sfence_vma,	false, false },
};
#endif

#if defined(STRESS_ARCH_S390) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_S390_PTLB)
#define HAVE_PRIV_INSTR
static void stress_s390_ptlb(void)
{
	__asm__ __volatile__("ptlb" : : : "memory");
}

static op_info_t op_info[] =
{
	 { "ptlb",	stress_s390_ptlb,	false, false },
};
#endif

#if defined(STRESS_ARCH_SH4) &&		\
    defined(HAVE_SIGLONGJMP) &&		\
    (defined(HAVE_ASM_SH4_RTE) ||	\
     defined(HAVE_ASM_SH4_SLEEP))
#define HAVE_PRIV_INSTR

#if defined(HAVE_ASM_SH4_RTE)
static void stress_sh4_rte(void)
{
	__asm__ __volatile__("rte");
}
#endif

#if defined(HAVE_ASM_SH4_SLEEP)
static void stress_sh4_sleep(void)
{
	__asm__ __volatile__("sleep");
}
#endif

static op_info_t op_info[] =
{
#if defined(HAVE_ASM_SH4_RTE)
	{ "rte",	stress_sh4_rte,		false, false },
#endif
#if defined(HAVE_ASM_SH4_SLEEP)
	{ "sleep",	stress_sh4_sleep,	false, false },
#endif
};
#endif

#if defined(STRESS_ARCH_SPARC) &&	\
    defined(HAVE_SIGLONGJMP) &&		\
    defined(HAVE_ASM_SPARC_RDPR)
#define HAVE_PRIV_INSTR
static void stress_sparc_rdpr(void)
{
	unsigned long int ver;

	__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
}

static op_info_t op_info[] =
{
	{ "rdpr",	stress_sparc_rdpr,	false, false },
};
#endif

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_SIGLONGJMP) &&		\
    (defined(HAVE_ASM_X86_CLTS) ||	\
     defined(HAVE_ASM_X86_HLT) ||	\
     defined(HAVE_ASM_X86_INVD) || 	\
     defined(HAVE_ASM_X86_INVLPG) ||	\
     defined(HAVE_ASM_X86_LGDT) || 	\
     defined(HAVE_ASM_X86_LLDT) || 	\
     defined(HAVE_ASM_X86_LMSW) || 	\
     defined(HAVE_ASM_X86_MOV_CR0) ||	\
     defined(HAVE_ASM_X86_MOV_DR0) ||	\
     defined(HAVE_ASM_X86_RDMSR) ||	\
     defined(HAVE_ASM_X86_RDPMC) ||	\
     defined(HAVE_ASM_X86_WBINVD) ||	\
     defined(HAVE_ASM_X86_WRMSR))
#define HAVE_PRIV_INSTR
#define HAVE_PRIV_PAGE

static void *page;

#if defined(HAVE_ASM_X86_CLTS)
static void stress_x86_clts(void)
{
	__asm__ __volatile__("clts");
}
#endif

#if defined(HAVE_ASM_X86_HLT)
static void stress_x86_hlt(void)
{
	__asm__ __volatile__("hlt");
}
#endif

#if defined(HAVE_ASM_X86_INVD)
static void stress_x86_invd(void)
{
	__asm__ __volatile__("invd");
}
#endif

#if defined(HAVE_ASM_X86_INVLPG)
static void stress_x86_invlpg(void)
{
	if (page != MAP_FAILED)
		__asm__ __volatile__("invlpg (%0)" ::"r" (page) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_LGDT)
static void stress_x86_lgdt(void)
{
	if (page != MAP_FAILED)
		__asm__ __volatile__("lgdt (%0)" ::"r" (page));
}
#endif

#if defined(HAVE_ASM_X86_LLDT)
static void stress_x86_lldt(void)
{
	uint16_t src = 0;

	__asm__ __volatile__("lldt %0" ::"r" (src));
}
#endif

#if defined(HAVE_ASM_X86_LMSW)
static void stress_x86_lmsw(void)
{
	uint16_t src = 0;

	__asm__ __volatile__("lmsw %0" ::"r" (src));
}
#endif

#if defined(HAVE_ASM_X86_MOV_CR0)
static void stress_x86_mov_cr0(void)
{
	unsigned long int cr0;

	__asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0) : : "memory");
}
#endif

#if defined(HAVE_ASM_X86_MOV_DR0)
static void stress_x86_mov_dr0(void)
{
	unsigned long int dr0;

	__asm__ __volatile__("mov %%dr0, %0" : "=r"(dr0) : : "memory");
}
#endif

#if defined(HAVE_ASM_X86_RDMSR)
static void stress_x86_rdmsr(void)
{
	uint32_t msr = 0xc0000080;	/* extended feature register */
	uint32_t lo;
	uint32_t hi;

	__asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
}
#endif

#if defined(HAVE_ASM_X86_RDPMC)
static void stress_x86_rdpmc(void)
{
        uint32_t lo, hi, counter = 0;

        __asm__ __volatile__("rdpmc" : "=a" (lo), "=d" (hi) : "c" (counter));
}
#endif

#if defined(HAVE_ASM_X86_WBINVD)
static void stress_x86_wbinvd(void)
{
	__asm__ __volatile__("wbinvd");
}
#endif

#if defined(HAVE_ASM_X86_WRMSR)
static void stress_x86_wrmsr(void)
{
	uint32_t msr = 0xc0000080;	/* extended feature register */
	uint32_t lo = 0;
	uint32_t hi = 0;

	__asm__ __volatile__("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}
#endif


static op_info_t op_info[] =
{
#if defined(HAVE_ASM_X86_CLTS)
	{ "clts",	stress_x86_clts,	false, false },
#endif
#if defined(HAVE_ASM_X86_HLT)
	{ "hlt",	stress_x86_hlt,		false, false },
#endif
#if defined(HAVE_ASM_X86_INVD)
	{ "invd",	stress_x86_invd,	false, false },
#endif
#if defined(HAVE_ASM_X86_INVLPG)
	{ "invlpg",	stress_x86_invlpg,	false, false },
#endif
#if defined(HAVE_ASM_X86_LGDT)
	{ "lgdt",	stress_x86_lgdt,	false, false },
#endif
#if defined(HAVE_ASM_X86_LLDT)
	{ "lldt",	stress_x86_lldt,	false, false },
#endif
#if defined(HAVE_ASM_X86_LMSW)
	{ "lmsw",	stress_x86_lmsw,	false, false },
#endif
#if defined(HAVE_ASM_X86_MOV_CR0)
	{ "mov cr0",	stress_x86_mov_cr0,	false, false },
#endif
#if defined(HAVE_ASM_X86_MOV_DR0)
	{ "mov dr0",	stress_x86_mov_dr0,	false, false },
#endif
#if defined(HAVE_ASM_X86_RDMSR)
	{ "rdmsr",	stress_x86_rdmsr,	false, false },
#endif
#if defined(HAVE_ASM_X86_RDPMC)
	{ "rdpmc",	stress_x86_rdpmc,	false, false },
#endif
#if defined(HAVE_ASM_X86_WBINVD)
	{ "wbinvd",	stress_x86_wbinvd,	false, false },
#endif
#if defined(HAVE_ASM_X86_WRMSR)
	{ "wrmsr",	stress_x86_wrmsr,	false, false },
#endif
};
#endif

#if defined(HAVE_PRIV_INSTR)

static sigjmp_buf jmp_env;
static size_t idx = 0;
static double t_start, duration, count;

static inline void stress_sigsegv_handler(int signum)
{
	(void)signum;

	duration += stress_time_now() - t_start;
	count += 1.0;
	op_info[idx].trapped = true;
	idx++;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

#if defined(SIGILL) ||	\
    defined(SIGBUS)
static void stress_sigill_handler(int signum)
{
	op_info[idx].invalid = true;
	stress_sigsegv_handler(signum);
}
#endif

/*
 *  stress_priv_instr()
 *      stress privileged instructions
 */
static int stress_priv_instr(stress_args_t *args)
{
	size_t i, len;
	int ret;
	double rate;

	idx = 0;
	duration = 0.0;
	count = 0.0;

#if defined(HAVE_PRIV_PAGE)
	page = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (page != MAP_FAILED)
		stress_set_vma_anon_name(page, args->page_size, "priv-page");
#endif
	if (stress_sighandler(args->name, SIGSEGV, stress_sigsegv_handler, NULL))
		return EXIT_NO_RESOURCE;
#if defined(SIGILL)
	if (stress_sighandler(args->name, SIGILL, stress_sigill_handler, NULL))
		return EXIT_NO_RESOURCE;
#endif
#if defined(SIGBUS)
	if (stress_sighandler(args->name, SIGBUS, stress_sigill_handler, NULL))
		return EXIT_NO_RESOURCE;
#endif

	for (i = 0; i < SIZEOF_ARRAY(op_info); i++) {
		op_info[i].invalid = false;
		op_info[i].trapped = false;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ret = sigsetjmp(jmp_env, 1);
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
	} while (ret == 1);

	do {
		if (idx >= SIZEOF_ARRAY(op_info))
			idx = 0;

		stress_bogo_inc(args);
		if (op_info[idx].op_func) {
			t_start = stress_time_now();
			op_info[idx].op_func();
		}
		idx++;
	} while (stress_continue(args));

finish:
        stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Get an overestimated buffer length */
	for (len = 0, i = 0; i < SIZEOF_ARRAY(op_info); i++) {
		if (!op_info[i].trapped)
			len += strlen(op_info[i].instr) + 3;
	}

	if (len > 0) {
		char *str;

		str = (char *)calloc(len, sizeof(*str));
		if (str) {
			int unhandled = 0;

			(void)shim_memset(str, 0, len);
			for (i = 0; i < SIZEOF_ARRAY(op_info); i++) {
				if (!op_info[i].trapped) {
					unhandled++;
					if (!*str)  {
						(void)shim_strscpy(str, op_info[i].instr, len);
					} else {
						(void)shim_strlcat(str, ", ", len);
						(void)shim_strlcat(str, op_info[i].instr, len);
					}
				}
			}
			pr_inf("%s: %d unhandled instructions: %s\n", args->name, unhandled, str);
			free(str);
		}
	}

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per privileged op trap",
		STRESS_DBL_NANOSECOND * rate, STRESS_METRIC_HARMONIC_MEAN);

#if defined(HAVE_PRIV_PAGE)
	if (page != MAP_FAILED)
		(void)munmap(page, args->page_size);
#endif
	if ((stress_bogo_get(args) > 1) && (count < 1.0)) {
		pr_fail("%s: attempted to execute %" PRIu64 " privileged instructions, trapped none.\n",
			args->name, stress_bogo_get(args));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_priv_instr_info = {
	.stressor = stress_priv_instr,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_priv_instr_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "no privileged op-code test for this architecture"
};
#endif
