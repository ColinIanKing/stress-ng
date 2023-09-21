// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"signest N",	 "start N workers generating nested signals" },
	{ NULL,	"signest-ops N", "stop after N bogo nested signals" },
	{ NULL,	NULL,		 NULL }
};

static bool jmp_env_ok;
static sigjmp_buf jmp_env;

static const int signals[] = {
#if defined(SIGHUP)
	SIGHUP,
#endif
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGQUIT)
	SIGQUIT,
#endif
#if defined(SIGABRT)
	SIGABRT,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGTERM)
	SIGTERM,
#endif
#if defined(SIGXCPU)
	SIGXCPU,
#endif
#if defined(SIGXFSZ)
	SIGXFSZ,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGSTKFLT)
	SIGSTKFLT,
#endif
#if defined(SIGPWR)
	SIGPWR,
#endif
#if defined(SIGINFO)
	SIGINFO,
#endif
#if defined(SIGVTALRM)
	SIGVTALRM,
#endif
#if defined(SIGUSR1)
	SIGUSR1,
#endif
#if defined(SIGUSR2)
	SIGUSR2,
#endif
#if defined(SIGTTOU)
	SIGTTOU,
#endif
#if defined(SIGTTIN)
	SIGTTIN,
#endif
#if defined(SIGWINCH)
	SIGWINCH,
#endif
};

typedef struct {
	const stress_args_t *args;
	uint32_t signalled;	/* bitmap of index into signals[] handled */
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

static void stress_signest_ignore(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(signals); i++)
		VOID_RET(int, stress_sighandler("signest", signals[i], SIG_IGN, NULL));
}

static inline ssize_t stress_signest_find(int signum)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(signals); i++) {
		if (signals[i] == signum)
			return (ssize_t)i;
	}
	return (ssize_t)-1;
}

static void MLOCKED_TEXT stress_signest_handler(int signum)
{
	ssize_t i;
	const intptr_t addr = (intptr_t)&i;
	double run_time = stress_time_now() - signal_info.time_start;

	handled++;
	signal_info.depth++;
	/* After a while this becomes more unlikely than likely */
	if (UNLIKELY(signal_info.depth > signal_info.max_depth))
		signal_info.max_depth = signal_info.depth;

	/* using alternative signal stack? */
	if ((addr >= signal_info.altstack_start) &&
	    (addr < signal_info.altstack_end)) {
		ptrdiff_t delta;

 		delta = signal_info.altstack - addr;
		if (delta < 0)
			delta = -delta;
		if (delta > signal_info.stack_depth)
			signal_info.stack_depth = delta;
	}

	if (UNLIKELY(run_time > (double)g_opt_timeout)) {
		stress_signest_ignore();
		if (jmp_env_ok)
			siglongjmp(jmp_env, 1);
	}

	if (UNLIKELY(signal_info.stop)) {
		stress_signest_ignore();
		if (jmp_env_ok)
			siglongjmp(jmp_env, 1);
	}

	if (UNLIKELY(!signal_info.args))
		goto done;

	stress_bogo_inc(signal_info.args);
	if (UNLIKELY(!stress_continue(signal_info.args))) {
		stress_signest_ignore();
		if (jmp_env_ok)
			siglongjmp(jmp_env, 1);
	}

	i = stress_signest_find(signum);
	if (UNLIKELY((i < 0) || (i == (ssize_t)SIZEOF_ARRAY(signals))))
		goto done;

	signal_info.signalled |= 1U << i;

	for (; i < (ssize_t)SIZEOF_ARRAY(signals); i++) {
		if (signal_info.stop || !stress_continue(signal_info.args)) {
			if (jmp_env_ok)
				siglongjmp(jmp_env, 1);
		}
		(void)shim_raise(signals[i]);
		raised++;
	}

done:
	--signal_info.depth;
	return;
}

/*
 *  stress_signest
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_signest(const stress_args_t *args)
{
	size_t i, sz;
	int n, ret, rc;
	uint8_t *altstack;
	char *buf, *ptr;
	const size_t altstack_size = stress_get_min_sig_stack_size() * SIZEOF_ARRAY(signals);
	double rate;
	NOCLOBBER double t, duration;

	raised = 0;
	handled = 0;
	jmp_env_ok = false;

	altstack = (uint8_t*)mmap(NULL, altstack_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (altstack == MAP_FAILED) {
		pr_inf_skip("%s: cannot allocate alternative signal stack, "
			"errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_sigaltstack(altstack, altstack_size) < 0)
		return EXIT_FAILURE;

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

	for (i = 0; i < SIZEOF_ARRAY(signals); i++) {
		if (stress_sighandler(args->name, signals[i], stress_signest_handler, NULL) < 0)
			return EXIT_NO_RESOURCE;
	}

	jmp_env_ok = true;
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		(void)shim_raise(signals[0]);
		raised++;
	} while (stress_continue(args));

finish:
	duration = stress_time_now() - t;
	jmp_env_ok = false;
	signal_info.stop = true;
	stress_signest_ignore();

	for (sz = 1, n = 0, i = 0; i < SIZEOF_ARRAY(signals); i++) {
		if (signal_info.signalled & (1U << i)) {
			const char *name = stress_get_signal_name(signals[i]);

			n++;
			sz += name ? (strlen(name) + 1) : 32;
		}
	}

	if (args->instance == 0) {
		buf = calloc(sz, sizeof(*buf));
		if (buf) {
			for (ptr = buf, i = 0; i < SIZEOF_ARRAY(signals); i++) {
				if (signal_info.signalled & (1U << i)) {
					const char *name = stress_get_signal_name(signals[i]);

					if (name) {
						if (strncmp(name, "SIG", 3) == 0)
							name += 3;
						ptr += snprintf(ptr, (buf + sz - ptr), " %s", name);
					} else {
						ptr += snprintf(ptr, (buf + sz - ptr), " SIG%d", signals[i]);
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
	stress_metrics_set(args, 0, "nanosec to handle a signal", rate * 1000000000.0);

	stress_sigaltstack_disable();
	(void)munmap((void *)altstack, altstack_size);

	return rc;
}

stressor_info_t stress_signest_info = {
	.stressor = stress_signest,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
