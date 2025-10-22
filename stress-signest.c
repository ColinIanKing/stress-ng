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
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"signest N",	 "start N workers generating nested signals" },
	{ NULL,	"signest-ops N", "stop after N bogo nested signals" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SIGLONGJMP)

#define MAX_SIGNALS	(64)	/* Note: must NOT be larger than 8 * sizeof(signal_info.signalled) */

static bool jmp_env_ok;
static sigjmp_buf jmp_env;

static const int defined_signals[] = {
#if defined(SIGABRT)
	SIGABRT,
#endif
#if defined(SIGALRM)
	SIGALRM,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGCHLD)
	SIGCHLD,
#endif
#if defined(SIGCONT)
	SIGCONT,
#endif
#if defined(SIGEMT)
	SIGEMT,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGHUP)
	SIGHUP,
#endif
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGINFO)
	SIGINFO,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGIO)
	SIGIO,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGLOST)
	SIGLOST,
#endif
#if defined(SIGPIPE)
	SIGPIPE,
#endif
#if defined(SIGPOLL)
	SIGPOLL,
#endif
#if defined(SIGPROF)
	SIGPROF,
#endif
#if defined(SIGPWR)
	SIGPWR,
#endif
#if defined(SIGQUIT)
	SIGQUIT,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGSTKFLT)
	SIGSTKFLT,
#endif
#if defined(SIGSYS)
	SIGSYS,
#endif
#if defined(SIGTERM)
	SIGTERM,
#endif
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGTSTP)
	SIGTSTP,
#endif
#if defined(SIGTTIN)
	SIGTTIN,
#endif
#if defined(SIGTTOU)
	SIGTTOU,
#endif
#if defined(SIGUNUSED)
	SIGUNUSED,
#endif
#if defined(SIGURG)
	SIGURG,
#endif
#if defined(SIGUSR1)
	SIGUSR1,
#endif
#if defined(SIGUSR2)
	SIGUSR2,
#endif
#if defined(SIGVTALRM)
	SIGVTALRM,
#endif
#if defined(SIGWINCH)
	SIGWINCH,
#endif
#if defined(SIGXCPU)
	SIGXCPU,
#endif
#if defined(SIGXFSZ)
	SIGXFSZ,
#endif
};

typedef struct {
	int signum;		/* signal number */
	bool signalled;		/* true = signal handled */
} stress_signal_t;

static stress_signal_t signals[MAX_SIGNALS] ALIGN64;
static size_t max_signals;

typedef struct {
	stress_args_t *args;
	bool stop;		/* true to stop further nested signalling */
	intptr_t altstack;	/* alternative stack push start */
	intptr_t altstack_start;/* alternative start mmap start */
	intptr_t altstack_end;	/* alternative start mmap end */
	ptrdiff_t stack_depth;	/* approx stack depth */
	int depth;		/* call depth */
	int max_depth;		/* max call depth */
	double time_start;	/* start time */
} stress_signest_info_t;

static volatile stress_signest_info_t signal_info;
static uint64_t raised;
static uint64_t handled;
static size_t signal_index;

static void stress_signest_ignore(void)
{
	size_t i;

	for (i = 0; i < max_signals; i++)
		VOID_RET(int, stress_sighandler("signest", signals[i].signum, SIG_IGN, NULL));
}

static void MLOCKED_TEXT stress_signest_handler(int signum)
{
	const int i = signum;
	const intptr_t addr = (intptr_t)&i;
	const double run_time = stress_time_now() - signal_info.time_start;

	handled++;
	signal_info.depth++;
	/* After a while this becomes more unlikely than likely */
	if (UNLIKELY(signal_info.depth > signal_info.max_depth))
		signal_info.max_depth = signal_info.depth;

	/* using alternative signal stack? */
	if ((addr >= signal_info.altstack_start) &&
	    (addr < signal_info.altstack_end)) {
		ptrdiff_t delta = signal_info.altstack - addr;

		if (delta < 0)
			delta = -delta;
		if (delta > signal_info.stack_depth)
			signal_info.stack_depth = delta;
	}

	if (UNLIKELY(run_time > (double)g_opt_timeout)) {
		stress_signest_ignore();
		if (jmp_env_ok) {
			siglongjmp(jmp_env, 1);
			stress_no_return();
		}
	}

	if (UNLIKELY(signal_info.stop)) {
		stress_signest_ignore();
		if (jmp_env_ok) {
			siglongjmp(jmp_env, 1);
			stress_no_return();
		}
	}

	if (UNLIKELY(!signal_info.args))
		goto done;

	stress_bogo_inc(signal_info.args);
	if (UNLIKELY(!stress_continue(signal_info.args))) {
		stress_signest_ignore();
		if (jmp_env_ok) {
			siglongjmp(jmp_env, 1);
			stress_no_return();
		}
	}

	signals[signal_index].signalled = true;
	signal_index++;
	if (UNLIKELY(signal_index >= max_signals))
		goto done;

	if (signal_info.stop || !stress_continue(signal_info.args)) {
		if (jmp_env_ok) {
			siglongjmp(jmp_env, 1);
			stress_no_return();
		}
	}
	(void)shim_raise(signals[signal_index].signum);
	raised++;
done:
	--signal_info.depth;
	return;
}

/*
 *  stress_signest_shuffle()
 *	randomly shuffle order of signals array
 */
static inline void stress_signest_shuffle(void)
{
	register size_t i;
	
	for (i = 0; i < max_signals; i++) {
		register size_t j = (size_t)stress_mwc32modn((uint32_t)max_signals);
		stress_signal_t tmp;

		tmp = signals[i];
		signals[i] = signals[j];
		signals[j] = tmp;
	}
}

static int stress_signest_cmp(const void *p1, const void *p2)
{
	const stress_signal_t *s1 = (const stress_signal_t *)p1;
	const stress_signal_t *s2 = (const stress_signal_t *)p2;

	return s1->signum - s2->signum;
}

/*
 *  stress_signest
 *	stress by raising next signal
 */
static int stress_signest(stress_args_t *args)
{
	size_t i, sz;
	int n, ret, rc;
	uint8_t *altstack;
	char *buf, *ptr;
	const size_t altstack_size = stress_get_min_sig_stack_size() * MAX_SIGNALS;
	double rate;
	NOCLOBBER double t, duration;

	raised = 0;
	handled = 0;
	jmp_env_ok = false;

	for (i = 0; i < SIZEOF_ARRAY(defined_signals); i++) {
		signals[i].signum = defined_signals[i];
		signals[i].signalled = false;
	}
	max_signals = i;
#if defined(SIGRTMIN) && 	\
    defined(SIGRTMAX)
	{
		int signum;

		for (signum = SIGRTMIN; (signum <= SIGRTMAX) && (i < MAX_SIGNALS); i++, signum++) {
			signals[i].signum = signum;
			signals[i].signalled = false;
		}
		max_signals = i;
	}
#endif

	/* Remove any duplicate signals */
	qsort(signals, max_signals, sizeof(stress_signal_t), stress_signest_cmp);
	for (i = 0; i < max_signals - 1; i++) {
		if (signals[i].signum == signals[i + 1].signum) {
			size_t j;

			for (j = i; j < max_signals - 1; j++)
				signals[j] = signals[j + 1];
			max_signals--;
		}
	}

	altstack = (uint8_t*)stress_mmap_populate(NULL, altstack_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (altstack == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte alternative signal stack%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, altstack_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(altstack, altstack_size, "altstack");

	if (stress_sigaltstack(altstack, altstack_size) < 0) {
		(void)munmap((void *)altstack, altstack_size);
		return EXIT_FAILURE;
	}

	signal_info.args = args;
	signal_info.stop = false;
	signal_info.altstack = (intptr_t)(stress_get_stack_direction() > 0 ?
		altstack : altstack + altstack_size);
	signal_info.altstack_start = (intptr_t)altstack;
	signal_info.altstack_end = (intptr_t)altstack + (intptr_t)altstack_size;
	signal_info.depth = 0;
	signal_info.time_start = stress_time_now();

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/* SIGALRM, SIGINT or finished flags met */
		goto finish;
	}

	for (i = 0; i < max_signals; i++) {
		if (stress_sighandler(args->name, signals[i].signum, stress_signest_handler, NULL) < 0)
			return EXIT_NO_RESOURCE;
	}

	jmp_env_ok = true;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		signal_index = 0;
		(void)shim_raise(signals[signal_index].signum);
		raised++;
		if (UNLIKELY((raised & 0x3f) == 0))
			stress_signest_shuffle();
	} while (stress_continue(args));

finish:
	duration = stress_time_now() - t;
	jmp_env_ok = false;
	signal_info.stop = true;
	stress_signest_ignore();

	for (sz = 1, n = 0, i = 0; i < max_signals; i++) {
		if (signals[i].signalled) {
			const char *name = stress_get_signal_name(signals[i].signum);

			n++;
			sz += name ? (strlen(name) + 1) : 32;
		}
	}

	qsort(signals, n, sizeof(stress_signal_t), stress_signest_cmp);
	if (stress_instance_zero(args)) {
		buf = (char *)calloc(sz, sizeof(*buf));
		if (buf) {
			for (ptr = buf, i = 0; i < max_signals; i++) {
				if (signals[i].signalled) {
					const char *name = stress_get_signal_name(signals[i].signum);

					if (name) {
						if (strncmp(name, "SIG", 3) == 0)
							name += 3;
						ptr += snprintf(ptr, (buf + sz - ptr), " %s", name);
					} else {
						ptr += snprintf(ptr, (buf + sz - ptr), " SIG%d", signals[i].signum);
					}
				}
			}
			pr_inf("%s: %d unique nested signals handled,%s\n", args->name, n, buf);
			free(buf);
		} else {
			pr_inf("%s: %d unique nested signals handled\n", args->name, n);
		}
		if (signal_info.stack_depth) {
			pr_dbg("%s: stack depth %td bytes (~%td bytes per signal)\n",
				args->name, signal_info.stack_depth,
				signal_info.max_depth ? signal_info.stack_depth / signal_info.max_depth : 0);
		} else {
			pr_dbg("%s: stack depth unknown, didn't use alternative signal stack\n", args->name);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rc = EXIT_SUCCESS;
	if ((raised > 0) && (handled == 0)) {
		pr_fail("%s: %" PRIu64 " signals raised and no signals handled\n",
			args->name, raised);
		rc = EXIT_FAILURE;
	}
	rate = handled > 0 ? duration / (double)handled : 0.0;
	stress_metrics_set(args, 0, "nanosec to handle a signal",
		rate * 1000000000.0, STRESS_METRIC_HARMONIC_MEAN);

	stress_sigaltstack_disable();
	(void)munmap((void *)altstack, altstack_size);

	return rc;
}

const stressor_info_t stress_signest_info = {
	.stressor = stress_signest,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_signest_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
