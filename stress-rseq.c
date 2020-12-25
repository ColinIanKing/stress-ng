/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"rseq N",	"start N workers that exercise restartable sequences" },
	{ NULL,	"rseq-ops N",	"stop after N bogo restartable sequence operations" },
	{ NULL,	NULL,		NULL }
};


#if defined(HAVE_LINUX_RSEQ_H) &&	\
    defined(HAVE_ASM_NOP) &&		\
    defined(__NR_rseq)

#define STRESS_ACCESS_ONCE(x)     (*(__volatile__  __typeof__(x) *)&(x))

#if defined(NOPS)
#undef NOPS
#endif

#if !defined(OPTIMIZE0)
#define OPTIMIZE0       __attribute__((optimize("-O0")))
#endif

#define NOPS()		\
asm volatile(		\
	"nop\n"		\
	"nop\n"		\
	"nop\n"		\
	"nop\n"		\
);

static volatile struct rseq restartable_seq;	/* rseq */
static volatile uint64_t signal_segv;		/* sigsegv count */
static uint64_t n, crit_interruptions;		/* critical section interruption count */
static sigjmp_buf jmp_env;			/* sigsegv jmpbuf */

STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static inline void set_rseq_ptr64(uint64_t value)
{
	(void)memcpy((void *)&restartable_seq.rseq_cs, &value, sizeof(value));
}

static inline void set_rseq_zero(void)
{
	(void)memset((void *)&restartable_seq, 0, sizeof(restartable_seq));
}

STRESS_PRAGMA_POP

#define set_rseq_ptr(value)	set_rseq_ptr64((uint64_t)value)

/*
 *  shim_rseq()
 *	shim wrapper to rseq system call
 */
static int shim_rseq(volatile struct rseq *rseq_abi, uint32_t rseq_len,
			int flags, uint32_t sig)
{
	return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}

/*
 *  rseq_register
 *  	register rseq critical section and handler with a
 *	a signature of the 32 bits before the handler. This is
 *	abusing the spirit of the signature but it makes the
 *	critical section code easier to write in pure C rather
 *	have to hand craft per-architecture specific code
 */
static int rseq_register(uint32_t signature)
{
	set_rseq_zero();
	return shim_rseq(&restartable_seq, sizeof(restartable_seq), 0, signature);
}

/*
 *  rseq_unregister
 *	unregister the rseq handler code
 */
static int rseq_unregister(uint32_t signature)
{
	int rc;

	rc = shim_rseq(&restartable_seq, sizeof(restartable_seq), RSEQ_FLAG_UNREGISTER, signature);
	set_rseq_zero();
	return rc;
}

/*
 *  rseq_test()
 *	exercise a critical section. force zero optimization to try
 *	and keep the compiler from doing any code reorganization that
 *	may affect the intentional layout of the branch sections
 */
static int OPTIMIZE0 rseq_test(const uint32_t cpu, uint32_t *signature)
{
	register int i;
	struct rseq_cs __attribute__((aligned(32))) cs = {
		/* Version */
		0,
		/* Flags */
		0,
		/* Start of critical section */
		(uintptr_t)(void *)&&l1,
		/* Length of critical section */
		((uintptr_t)&&l2 - (uintptr_t)&&l1),
		/* Address of abort handler */
		(uintptr_t)&&l4
	};

	/*
	 *  setup phase, called once to find the
	 *  location of label l4 and the 32 bits before
	 *  it which acts as a 32 bit special magic signature
	 */
	if (signature) {
		uint32_t *ptr = &&l4;
		ptr--;
		*signature = *ptr;
		return 0;
	}

l1:
	/* Critical section starts here */
	set_rseq_ptr(&cs);
	if (cpu != restartable_seq.cpu_id)
		goto l4;

	/*
	 *  Long duration in crirical section will
	 *  be likely to be interrupted so rseq jumps
	 *  to label l4
	 */
	for (i = 0; i < 1000; i++) {
		NOPS();
	}
	set_rseq_ptr(NULL);
	return 0;
	/* Critical section ends here */
l2:
	/* No-op filler, should never execute */
	set_rseq_ptr(NULL);
	NOPS();
l4:
	/* Interrupted abort handler */
	set_rseq_ptr(NULL);
	return 1;
}

/*
 *  sigsegv_handler()
 *	if rseq critical section handling is broken
 *	(e.g. the signature differs from the expected
 *	pattern) then a SIGSEGV is triggered. Hence we
 *	require a handler for this
 */
static void sigsegv_handler(int sig)
{
	(void)sig;

	signal_segv++;
	siglongjmp(jmp_env, 1);
}

/*
 *  Sanity check of the rseq system call is available
 */
static int stress_rseq_supported(const char *name)
{
	uint32_t signature;

	rseq_test(-1, &signature);
	if (rseq_register(signature) < 0) {
		if (errno == ENOSYS) {
			pr_inf("%s stressor will be skipped, rseq system call not implemented\n",
				name);
		} else {
			pr_inf("%s stressor will be skipped, rseq system call failed to register, errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		return -1;
	}
	(void)rseq_unregister(signature);
	return 0;
}


/*
 *  stress_rseq()
 *	exercise restartable sequences rseq
 */
static int stress_rseq(const stress_args_t *args)
{
	uint32_t signature;
	struct sigaction sa;

	n = 0;
	crit_interruptions = 0;
	signal_segv = 0;

	(void)memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigsegv_handler;
	if (sigaction(SIGSEGV, &sa, NULL) < 0) {
		fprintf(stderr, "Failed to set SIGSEGV hhandler\n");
		exit(1);
	}

	rseq_test(-1, &signature);

	do {
		int ret;
		register int i;

		/*
		 *  bail out early if we trapped a rseq generated
		 *  SIGSEGV
		 */
		ret = sigsetjmp(jmp_env, 1);
		if (ret)
			break;

		/*
		 *  exercise register/critical section/unregister
		 */
		rseq_register(signature);
		for (i = 0; i < 10000; i++) {
			uint32_t cpu;

			cpu = STRESS_ACCESS_ONCE(restartable_seq.cpu_id_start);
			if (rseq_test(cpu, NULL))
				crit_interruptions++;
		}
		n += i;
		rseq_unregister(signature);
		inc_counter(args);
	} while (keep_stressing());

	pr_inf("%s: %" PRIu64 " critical section interruptions, %" PRIu64
	       " flag mismatches of %" PRIu64 " restartable sequences\n",
		args->name, crit_interruptions, signal_segv, n);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rseq_info = {
	.stressor = stress_rseq,
	.supported = stress_rseq_supported,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_rseq_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
