/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-bitops.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-pragma.h"

#include <math.h>

#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#endif

#if defined(HAVE_LINUX_FILTER_H)
#include <linux/filter.h>
#endif

#if defined(HAVE_LINUX_SECCOMP_H)
#include <linux/seccomp.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"opcode N",		"start N workers exercising random opcodes" },
	{ NULL,	"opcode-method M",	"set opcode stress method (M = random, inc, mixed, text)" },
	{ NULL,	"opcode-ops N",		"stop after N opcode bogo operations" },
	{ NULL, NULL,		   	NULL }
};

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(HAVE_LINUX_AUDIT_H) &&	\
    defined(HAVE_LINUX_FILTER_H) &&	\
    defined(HAVE_MPROTECT) &&		\
    defined(HAVE_SYS_PRCTL_H)

#if defined(__NR_rt_sigreturn)	&&	\
    defined(__NR_rt_sigprocmask)
#define STRESS_OPCODE_USE_SIGLONGJMP
static bool jmp_env_set;
static sigjmp_buf jmp_env;
#endif

#define SYSCALL_NR	(offsetof(struct seccomp_data, nr))

#define ALLOW_SYSCALL(syscall)					\
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_##syscall, 0, 1), 	\
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW)

#define PAGES		(16)

typedef void(*stress_opcode_func)(const size_t page_size, void *ops_begin,
				  const void *ops_end, const volatile uint64_t *op);

typedef struct {
	const char *name;
	const stress_opcode_func func;
} stress_opcode_method_info_t;

#if defined(NSIG)
#define MAX_SIGS	(NSIG)
#elif defined(_NSIG)
#define MAX_SIGS	(_NSIG)
#else
#define MAX_SIGS	(256)
#endif

typedef struct  {
	volatile uint64_t opcode;
	volatile uint64_t ops_attempted;
	volatile uint64_t ops_ok;
	volatile uint64_t count;
	volatile uint64_t opcode_prev;
	volatile uint64_t sig_count[MAX_SIGS];
} stress_opcode_state_t;

static stress_opcode_state_t *state;

static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGEMT)
	SIGEMT,
#endif
#if defined(SIGALRM)
	SIGALRM,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGHUP)
	SIGHUP,
#endif
#if defined(SIGSYS)
	SIGSYS
#endif
};

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(SECCOMP_SET_MODE_FILTER)
static struct sock_filter filter[] = {
	BPF_STMT(BPF_LD + BPF_W + BPF_ABS, SYSCALL_NR),
#if defined(__NR_exit)
	ALLOW_SYSCALL(exit),
#endif
#if defined(__NR_exit_group)
	ALLOW_SYSCALL(exit_group),
#endif
#if defined(__NR_rt_sigreturn)
	ALLOW_SYSCALL(rt_sigreturn),
#endif
#if defined(__NR_rt_sigprocmask)
	ALLOW_SYSCALL(rt_sigprocmask),
#endif
#if defined(__NR_mprotect)
	ALLOW_SYSCALL(mprotect),
#endif
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP)
};

static struct sock_fprog prog = {
	.len = (unsigned short int)SIZEOF_ARRAY(filter),
	.filter = filter
};
#endif

static void MLOCKED_TEXT stress_opcode_child_sighandler(int signum)
{
	if ((signum >= 0) && (signum < MAX_SIGS)) {
		volatile stress_opcode_state_t *vstate = (volatile stress_opcode_state_t *)state;

		(void)mprotect(state, sizeof(*state), PROT_READ | PROT_WRITE);
		vstate->sig_count[signum]++;
		(void)mprotect(state, sizeof(*state), PROT_READ);
	}

#if defined(STRESS_OPCODE_USE_SIGLONGJMP)
	/*
	 *  SIGINT, SIGALRM, SIGHUP are for termination,
	 *  SIGSEGV and SIGBUS we force exit in case state
	 *  is really messed up
	 */
	if ((signum == SIGINT) ||
	    (signum == SIGALRM) ||
	    (signum == SIGHUP) ||
	    (signum == SIGSEGV) ||
	    (signum == SIGBUS))
		_exit(1);

	/*
	 *  try to continue for less significant signals
	 *  such as SIGFPE and SIGTRAP
	 */
	if (jmp_env_set) {
		siglongjmp(jmp_env, 1);
		stress_no_return();
	}
#else
	_exit(1);
#endif
}

static inline void OPTIMIZE3 stress_opcode_random(
	const size_t page_size,
	void *ops_begin,
	const void *ops_end,
	const volatile uint64_t *op)
{
#if defined(STRESS_ARCH_X86)
	static const uint8_t x86_prefixes[] = {
		0xf0,	/* lock */
		0xf1,	/* repne/repnz */
		0xf3,	/* rep or repe/repz */
		0x2e,	/* CS segment override */
		0x36,	/* SS segment override */
		0x3e,	/* DS segment override */
		0x26,	/* ES segment override */
		0x64,	/* FS segment override */
		0x65,	/* GS segment override */
		0x2e,	/* branch not taken */
		0x3e,	/* branch taken */
		0x66,	/* operand size override */
		0x67,	/* address size override */
	};

	static const uint8_t x86_prefix_length[] = {
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
		2, 2, 2, 2, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 8, 14,
		15, 16, 17
	};
	uint32_t rnd;
#endif
	register uint32_t *ops32 = (uint32_t *)ops_begin;

	(void)op;
	(void)page_size;

PRAGMA_UNROLL_N(8)
	while (ops32 < (const uint32_t *)ops_end)
		*(ops32++) = stress_mwc32();

#if defined(STRESS_ARCH_X86)
	rnd = stress_mwc32();
	if ((rnd & 0xff) < 8) {
		register uint8_t *ops8 = (uint8_t *)ops_begin;
		int i, n = x86_prefix_length[stress_mwc8modn(SIZEOF_ARRAY(x86_prefix_length))];

		for (i = 0; (i < n) && (ops8 < (const uint8_t *)ops_end); i++)
			*(ops8++) = x86_prefixes[stress_mwc8modn(SIZEOF_ARRAY(x86_prefixes))];
	}
#endif
}

#if !defined(STRESS_OPCODE_SIZE)
/* Not defimed? Default to 64 bit */
#define STRESS_OPCODE_SIZE	(64)
#define STRESS_OPCODE_MASK	(0xffffffffffffffffULL)
#endif
#define OPCODE_HEX_DIGITS	(STRESS_OPCODE_SIZE >> 2)

static void OPTIMIZE3 stress_opcode_inc(
	const size_t page_size,
	void *ops_begin,
	const void *ops_end,
	const volatile uint64_t *op)
{
	switch (STRESS_OPCODE_SIZE) {
	case 8:	{
			register const uint8_t tmp8 = *op & 0xff;
			register uint8_t *ops = (uint8_t *)ops_begin;
			register ssize_t i = (ssize_t)page_size;

			while (i--) {
				*(ops++) = tmp8;
			}
		}
		break;
	case 16: {
			register const uint16_t tmp16 = *op & 0xffff;
			register uint16_t *ops = (uint16_t *)ops_begin;
			register ssize_t i = (ssize_t)(page_size >> 1);

			while (i--) {
				*(ops++) = tmp16;
			}
		}
		break;
	default:
	case 32: {
			register const uint32_t tmp32 = *op & 0xffffffffL;
			register uint32_t *ops = (uint32_t *)ops_begin;
			register size_t i = (ssize_t)(page_size >> 2);

			while (i--) {
				*(ops++) = tmp32;
			}
		}
		break;
	case 48:
		{
			register const uint64_t tmp64 = *op;
			register uint8_t *ops = (uint8_t *)ops_begin;
			register size_t i = (ssize_t)(page_size / 6);

			while (i--) {
				*(ops++) = (tmp64 >> 0);
				*(ops++) = (tmp64 >> 8);
				*(ops++) = (tmp64 >> 16);
				*(ops++) = (tmp64 >> 24);
				*(ops++) = (tmp64 >> 32);
				*(ops++) = (tmp64 >> 40);
			}
			/* There is some slop at the end */
			while (ops < (const uint8_t *)ops_end)
				*ops++ = 0x00;
		}
		break;
	case 64: {
			register const uint64_t tmp64 = *op;
			register uint64_t *ops = (uint64_t *)ops_begin;
			register size_t i = (ssize_t)(page_size >> 3);

			while (i--)
				*(ops++) = tmp64;
		}
		break;
	}
}

static void OPTIMIZE3 stress_opcode_mixed(
	const size_t page_size,
	void *ops_begin,
	const void *ops_end,
	const volatile uint64_t *op)
{
	register const uint64_t tmp = *op;
	register uint64_t *ops = (uint64_t *)ops_begin;

	(void)page_size;
	while (ops < (const uint64_t *)ops_end) {
		register const uint64_t rnd = stress_mwc64();

		*(ops++) = tmp;
		*(ops++) = tmp ^ 0xffffffff;	/* Inverted */
		*(ops++) = ((tmp >> 1) ^ tmp);	/* Gray */
		*(ops++) = stress_reverse64(tmp);

		*(ops++) = rnd;
		*(ops++) = rnd ^ 0xffffffff;
		*(ops++) = ((rnd >> 1) ^ rnd);
		*(ops++) = stress_reverse64(rnd);
	}
}

static void stress_opcode_text(
	const size_t page_size,
	void *ops_begin,
	const void *ops_end,
	const volatile uint64_t *op)
{
	char *text_start, *text_end;
	const size_t ops_len = (uintptr_t)ops_end - (uintptr_t)ops_begin;
	size_t text_len = stress_exec_text_addr(&text_start, &text_end);
	uint8_t *ops;
	size_t offset;

	/*
	 *  Don't access last 8 bytes to avoid 64 bit accesses off
	 *  the end.
	 */
	if (text_len <= 8)
		return;
	text_len -= 8;

	if (text_len < ops_len) {
		stress_opcode_random(page_size, ops_begin, ops_end, op);
		return;
	}

	offset = stress_mwc64modn(text_len - ops_len) & ~(0x7ULL);
	(void)shim_memcpy(ops_begin, text_start + offset, ops_len);
	for (ops = (uint8_t *)ops_begin; ops < (const uint8_t *)ops_end; ops++) {
		register const uint8_t rnd = stress_mwc8();

		/* 1 in 8 chance of random bit corruption */
		if (rnd < 32) {
			register const uint8_t bit = (uint8_t)(1 << (rnd & 7));

			*ops ^= bit;
		}
	}
}

static const stress_opcode_method_info_t stress_opcode_methods[] = {
	{ "random",	stress_opcode_random },
	{ "text",	stress_opcode_text },
	{ "inc",	stress_opcode_inc },
	{ "mixed",	stress_opcode_mixed },
};

/*
 *  stress_opcode
 *	stress with random opcodes
 */
static int stress_opcode(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	int rc;
	size_t i, opcode_method = 0;
	const stress_opcode_method_info_t *method;
#if STRESS_OPCODE_SIZE >= 8
	const size_t opcode_bytes = STRESS_OPCODE_SIZE >> 3;
#else
	const size_t opcode_bytes = 1;
#endif
	const size_t opcode_loops = page_size / opcode_bytes;
	double op_start, rate, t, duration, percent;
	const double num_opcodes = pow(2.0, STRESS_OPCODE_SIZE);
	uint64_t forks = 0;
	void *opcodes;
	/*
	 *  vstate is a volatile alias to state to force non-register
	 *  usage of state and hence to try to avoid clobbering by
	 *  an invalid opcode. Same applies for all the elements
	 *  in the structure.
	 */
	volatile stress_opcode_state_t *vstate;

	state = (stress_opcode_state_t *)
		stress_mmap_populate(NULL, sizeof(*state),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (state == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zu bytes failed%s, errno=%d (%s) "
			"skipping stressor\n",
			args->name, args->page_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(state, sizeof(*state), "state");
	vstate = (volatile stress_opcode_state_t *)state;

	opcodes = (void *)stress_mmap_populate(NULL, page_size * (PAGES + 2),
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (opcodes == MAP_FAILED) {
		pr_fail("%s: mmap of %zu bytes failed%s, errno=%d (%s)\n",
			args->name, page_size * (PAGES + 2),
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap((void *)state, sizeof(*state));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(opcodes, page_size * (PAGES + 2), "opcodes");
	/* Force pages resident */
	(void)shim_memset(opcodes, 0x00, page_size * (PAGES + 2));

	(void)stress_get_setting("opcode-method", &opcode_method);
	method = &stress_opcode_methods[opcode_method];

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	op_start = (num_opcodes * (double)args->instance) / args->instances;
	vstate->opcode = (uint64_t)op_start;

	t = stress_time_now();
	do {
		pid_t pid;

		/*
		 *  Force a new random value so that child always
		 *  gets a different random value on each fork
		 */
		(void)stress_mwc32();
		if (method->func == stress_opcode_inc) {
			char buf[32];

			(void)snprintf(buf, sizeof(buf), "opcode-0x%*.*" PRIx64 " [run]",
				OPCODE_HEX_DIGITS, OPCODE_HEX_DIGITS, vstate->opcode);
			stress_set_proc_name(buf);
		}
again:
		jmp_env_set = false;
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		if (pid == 0) {
			struct itimerval it;
			const size_t ops_size = page_size * PAGES;
			void *ops_begin = (void *)((uint8_t *)opcodes + page_size);
			void *ops_end = (void *)((uint8_t *)ops_begin + ops_size);
			NOCLOBBER void *ops_ptr;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)sched_settings_apply(true);

			for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
				if (stress_sighandler(args->name, sigs[i], stress_opcode_child_sighandler, NULL) < 0)
					_exit(EXIT_FAILURE);
			}

			/* We don't want bad ops clobbering this region */
			stress_shared_readonly();

			/* We don't want core dumps either */
			stress_process_dumpable(false);

			/* Drop all capabilities */
			if (stress_drop_capabilities(args->name) < 0) {
				_exit(EXIT_NO_RESOURCE);
			}

			(void)mprotect((void *)opcodes, page_size, PROT_NONE);
			(void)mprotect((void *)ops_end, page_size, PROT_NONE);
			(void)mprotect((void *)ops_begin, ops_size, PROT_WRITE);

			/* Populate with opcodes */
			method->func(page_size, ops_begin, ops_end, &vstate->opcode);

			/* Make read-only executable and force I$ flush */
			(void)mprotect((void *)ops_begin, ops_size, PROT_READ | PROT_EXEC);
			shim_flush_icache((char *)ops_begin, (char *)ops_end);

			stress_parent_died_alarm();

			/*
			 * Force timeout abort if the opcodes magically
			 * do an infinite loop
			 */
			it.it_interval.tv_sec = 0;
			it.it_interval.tv_usec = 15000;
			it.it_value.tv_sec = 0;
			it.it_value.tv_usec = 15000;
			if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
				pr_fail("%s: setitimer failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_NO_RESOURCE);
			}

			/* Disable stack smashing messages */
			stress_set_stack_smash_check_flag(false);

			/*
			 * close stdio fds, we
			 * really don't care if the child dies
			 * in a bad way and libc or whatever
			 * reports of stack smashing or heap
			 * corruption since the child will
			 * die soon anyhow
			 */
			(void)fflush(stdout);
			(void)fflush(stderr);

			(void)fclose(stdin);
			(void)fclose(stdout);
			(void)fclose(stderr);

#if defined(STRESS_OPCODE_USE_SIGLONGJMP)
			{
				int ret;

				ret = sigsetjmp(jmp_env, 1);
				if (ret == 1)
					goto exercise;
				jmp_env_set = true;
			}
#endif
#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(SECCOMP_SET_MODE_FILTER)
			/*
			 *  Limit syscall using seccomp
			 */
			(void)shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
#endif

#if defined(STRESS_OPCODE_USE_SIGLONGJMP)
exercise:
#endif
			for (i = 0, ops_ptr = ops_begin; i < opcode_loops; i++) {
				vstate->opcode = (vstate->opcode + 1) & STRESS_OPCODE_MASK;
				vstate->ops_attempted++;
				(void)mprotect((void *)state, sizeof(*state), PROT_READ);

				((void (*)(void))(ops_ptr))();

				(void)mprotect((void *)state, sizeof(*state), PROT_READ | PROT_WRITE);
				ops_ptr = (void *)((uintptr_t)ops_ptr + opcode_bytes);
				/* Check if got stuck */
				if (vstate->opcode_prev == vstate->opcode)
					break;
				vstate->opcode_prev = vstate->opcode;
				vstate->ops_ok++;
			}
			_exit(0);
		}
		if (pid > 0) {
			pid_t ret;
			int status;

			forks++;
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX" failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				(void)stress_kill_pid_wait(pid, NULL);
			}
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));

finish:
	duration = stress_time_now() - t;
	rc = EXIT_SUCCESS;

	if ((duration > 0.0) &&
	    (vstate->ops_attempted > 0.0) &&
	    stress_instance_zero(args) &&
	    (args->instances > 0)) {
		const double secs_in_tropical_year = 365.2422 * 24.0 * 60.0 * 60.0;
		double estimated_duration = (duration * num_opcodes / vstate->ops_attempted) / args->instances;

		if (estimated_duration > secs_in_tropical_year * 5) {
			estimated_duration = round(estimated_duration / secs_in_tropical_year);
			pr_dbg("%s estimated time to cover all op-codes: %.0f years\n",
				args->name, estimated_duration);
		} else if (estimated_duration < 1.0) {
			pr_dbg("%s estimated time to cover all op-codes: %.4f seconds\n",
				args->name, estimated_duration);
		} else {
			pr_dbg("%s: estimated time to cover all op-codes: %s\n",
				args->name, stress_duration_to_str(estimated_duration, false, false));
		}
	}

	rate = (duration > 0.0) ? (double)vstate->ops_attempted / duration : 0.0;
	stress_metrics_set(args, 0, "opcodes exercised per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->ops_ok / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 1, "% opcodes successfully executed",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->sig_count[SIGILL] / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 2, "% illegal opcodes executed",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->sig_count[SIGBUS] / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 3, "% opcodes generated SIGBUS",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->sig_count[SIGSEGV] / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 4, "% opcodes generated SIGSEGV",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->sig_count[SIGFPE] / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 5, "% opcodes generated SIGFPE",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	percent = (vstate->ops_attempted > 0.0) ? 100.0 * (double)vstate->sig_count[SIGTRAP] / (double)vstate->ops_attempted : 0.0;
	stress_metrics_set(args, 6, "% opcodes generated SIGTRAP",
		percent, STRESS_METRIC_GEOMETRIC_MEAN);
	rate = (duration > 0.0) ? (double)forks / duration : 0.0;
	stress_metrics_set(args, 7, "forks per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)opcodes, page_size * (PAGES + 2));
	(void)munmap((void *)state, sizeof(*state));
	return rc;
}

static const char *stress_opcode_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_opcode_methods)) ? stress_opcode_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_opcode_method, "opcode-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_opcode_method },
	END_OPT,
};

const stressor_info_t stress_opcode_info = {
	.stressor = stress_opcode,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.help = help
};
#else

static const stress_opt_t opts[] = {
	{ OPT_opcode_method, "opcode-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

const stressor_info_t stress_opcode_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without linux/seccomp.h, linux/audit.h, linux/filter.h, sys/prctl.h or mprotect()"
};
#endif
