/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

#if defined(HAVE_LINUX_RSEQ_H)
#include <linux/rseq.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"rseq N",	"start N workers that exercise restartable sequences" },
	{ NULL,	"rseq-ops N",	"stop after N bogo restartable sequence operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_RSEQ_H) &&		\
    defined(HAVE_ASM_NOP) &&			\
    defined(__NR_rseq) &&			\
    defined(HAVE_SYSCALL) &&			\
    defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    !defined(HAVE_COMPILER_CLANG) &&		\
    !defined(HAVE_COMPILER_ICC) &&		\
    !defined(HAVE_COMPILER_ICX)

#define STRESS_ACCESS_ONCE(x)     (*(__volatile__  __typeof__(x) *)&(x))

#if defined(NOPS)
#undef NOPS
#endif

#if !defined(OPTIMIZE0)
#define OPTIMIZE0       __attribute__((optimize("-O0")))
#endif

#define NOPS()	do {		\
	stress_asm_nop();	\
	stress_asm_nop();	\
	stress_asm_nop();	\
	stress_asm_nop();	\
} while (0);

typedef struct {
	uint64_t crit_count;		/* critical path entry count */
	uint64_t crit_interruptions;	/* interruptions in critical path */
	uint64_t segv_count;		/* SIGSEGV count from rseq */
	uint32_t valid_signature;	/* rseq valid signature */
} rseq_info_t;

static struct rseq restartable_seq;	/* rseq */
static uint32_t valid_signature;
static rseq_info_t *rseq_info;

STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static inline void set_rseq_ptr64(uint64_t value)
{
	(void)shim_memcpy((void *)&restartable_seq.rseq_cs, &value, sizeof(value));
}

static inline void set_rseq_zero(void)
{
	(void)shim_memset((void *)&restartable_seq, 0, sizeof(restartable_seq));
}

STRESS_PRAGMA_POP

#define set_rseq_ptr(value)	set_rseq_ptr64((uint64_t)(intptr_t)value)

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
static int rseq_register(volatile struct rseq *rseq, uint32_t signature)
{
	set_rseq_zero();
	return shim_rseq(rseq, sizeof(*rseq), 0, signature);
}

/*
 *  rseq_unregister
 *	unregister the rseq handler code
 */
static int rseq_unregister(volatile struct rseq *rseq, uint32_t signature)
{
	int rc;

	rc = shim_rseq(rseq, sizeof(*rseq), RSEQ_FLAG_UNREGISTER, signature);
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
	 *  Long duration in critical section will
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

	rseq_info->segv_count++;
}

/*
 *  Sanity check of the rseq system call is available
 */
static int stress_rseq_supported(const char *name)
{
	uint32_t signature;

	rseq_test(-1, &signature);
	if (rseq_register(&restartable_seq, signature) < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s stressor will be skipped, rseq system call not implemented\n",
				name);
		} else {
			pr_inf_skip("%s stressor will be skipped, rseq system call failed to register, errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		return -1;
	}
	(void)rseq_unregister(&restartable_seq, signature);
	return 0;
}

/*
 *  stress_rseq()
 *	exercise restartable sequences rseq
 */
static int stress_rseq_oomable(const stress_args_t *args, void *context)
{
	struct sigaction sa;
	(void)context;
	char misaligned_seq_buf[sizeof(struct rseq) + 1];
	struct rseq *misaligned_seq = (struct rseq *)&misaligned_seq_buf[1];
	struct rseq invalid_seq;

	(void)shim_memcpy((void *)misaligned_seq, (void *)&restartable_seq, sizeof(restartable_seq));
	(void)shim_memcpy((void *)&invalid_seq, (void *)&restartable_seq, sizeof(restartable_seq));

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigsegv_handler;
	if (sigaction(SIGSEGV, &sa, NULL) < 0) {
		pr_inf("%s: failed to set SIGSEGV handler\n", args->name);
		_exit(EXIT_FAILURE);
	}

	do {
		register int i;
		int ret;

		/*
		 *  exercise register/critical section/unregister,
		 *  every 2048 register with an invalid signature to
		 *  exercise kernel invalid signature check
		 */
		if ((stress_bogo_get(args) & 0x1fff) == 1)
			valid_signature = 0xbadc0de;
		else
			valid_signature = rseq_info->valid_signature;

		if (rseq_register(&restartable_seq, valid_signature) < 0) {
			if (errno != EINVAL)
				pr_err("%s: rseq failed to register: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			goto unreg;
		}

		for (i = 0; i < 10000; i++) {
			uint32_t cpu;
			cpu = STRESS_ACCESS_ONCE(restartable_seq.cpu_id_start);
			if (rseq_test(cpu, NULL))
				rseq_info->crit_interruptions++;
		}
		rseq_info->crit_count += i;

unreg:
		(void)rseq_unregister(&restartable_seq, valid_signature);
		stress_bogo_inc(args);
		shim_sched_yield();

		/* Exercise invalid rseq calls.. */

		/* Invalid rseq size, EINVAL */
		ret = shim_rseq(&restartable_seq, 0, 0, valid_signature);
		if (ret == 0)
			(void)rseq_unregister(&restartable_seq, valid_signature);

		/* invalid flags */
		ret = shim_rseq(&restartable_seq, sizeof(invalid_seq), ~RSEQ_FLAG_UNREGISTER, valid_signature);
		if (ret == 0)
			(void)rseq_unregister(&restartable_seq, valid_signature);

		/* Invalid alignment, EINVAL */
		ret = rseq_register(misaligned_seq, valid_signature);
		if (ret == 0)
			(void)rseq_unregister(misaligned_seq, valid_signature);

		/* Invalid unregister, invalid struct size, EINVAL */
		(void)shim_rseq(&restartable_seq, 0, RSEQ_FLAG_UNREGISTER, valid_signature);

		/* Invalid unregister, different seq struct addr, EINVAL */
		(void)rseq_unregister(&invalid_seq, valid_signature);

		/* Invalid unregister, different signature, EINAL  */
		(void)rseq_unregister(&invalid_seq, ~valid_signature);

		/* Register twice, EBUSY */
		(void)rseq_register(&restartable_seq, valid_signature);
		(void)rseq_register(&restartable_seq, valid_signature);
		(void)rseq_unregister(&restartable_seq, valid_signature);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

static int stress_rseq(const stress_args_t *args)
{
	int ret;

	/*
	 *  rseq_info is in a shared page to avoid losing the
	 *  stats when the child gets SEGV'd by rseq when we use
	 *  in invalid signature
	 */
	rseq_info = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rseq_info == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate shared page, "
			"errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	/* Get the expected valid signature */
	rseq_test(-1, &rseq_info->valid_signature);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, NULL, stress_rseq_oomable, STRESS_OOMABLE_QUIET);
	pr_inf("%s: %" PRIu64 " critical section interruptions, %" PRIu64
	       " flag mismatches of %" PRIu64 " restartable sequences\n",
		args->name, rseq_info->crit_interruptions,
		rseq_info->segv_count, rseq_info->crit_count);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(rseq_info, args->page_size);

	return ret;
}

stressor_info_t stress_rseq_info = {
	.stressor = stress_rseq,
	.supported = stress_rseq_supported,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_rseq_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built without Linux restartable sequences support"
};
#endif
