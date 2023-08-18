// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"

static const stress_help_t help[] = {
	{ NULL,	"softlockup N",     "start N workers that cause softlockups" },
	{ NULL,	"softlockup-ops N", "stop after N softlockup bogo operations" },
	{ NULL,	NULL,		    NULL }
};

#if defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_SETSCHEDULER)

/*
 *  stress_softlockup_supported()
 *      check if we can run this as root
 */
static int stress_softlockup_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_NICE)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_NICE "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

typedef struct {
	const int	policy;
	int		max_prio;
	const char	*name;
} stress_policy_t;

static stress_policy_t policies[] = {
#if defined(SCHED_FIFO)
	{ SCHED_FIFO, 0, "SCHED_FIFO"},
#endif
#if defined(SCHED_RR)
	{ SCHED_RR, 0, "SCHED_RR" },
#endif
};

static sigjmp_buf jmp_env;
static volatile bool softlockup_start;

static NOINLINE void OPTIMIZE0 stress_softlockup_loop(const uint64_t loops)
{
	uint64_t i;

	for (i = 0; i < loops; i++) {
		stress_asm_nop();
		stress_asm_mb();
	}
}

/*
 *  stress_softlockup_loop_count()
 *	number of loops for 0.01 seconds busy wait delay
 */
static uint64_t OPTIMIZE0 stress_softlockup_loop_count(void)
{
	double t, d;
	uint64_t n = 1024 * 64, i;

	do {
		t = stress_time_now();
		for (i = 0; i < n; i++) {
			stress_asm_nop();
			stress_asm_mb();
		}
		d = stress_time_now() - t;
		if (d > 0.01)
			break;
		n = n + n;
	}  while (stress_continue_flag());

	return n;
}

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void MLOCKED_TEXT NORETURN stress_rlimit_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
	siglongjmp(jmp_env, 1);
}

/*
 *  drop_niceness();
 *	see how low we can go with niceness
 */
static void drop_niceness(void)
{
	int nice_val, i;

	errno = 0;
	nice_val = nice(0);

	/* Should never fail */
	if (errno)
		return;

	/*
	 *  Traditionally no more than -20, but see if we
	 *  can force it lower if we are originally running
	 *  at nice level 19
	 */
	for (i = 0; i < 40; i++) {
		int old_nice_val = nice_val;

		errno = 0;
		nice_val = nice(-1);
		if (errno)
			return;
		if (nice_val == old_nice_val)
			return;
	}
}

static void stress_softlockup_child(
	const stress_args_t *args,
	struct sched_param *param,
	const double start,
	const uint64_t timeout,
	const uint64_t loop_count)
{
	struct sigaction old_action_xcpu;
	struct rlimit rlim;
	const pid_t mypid = getpid();
	int ret;
	int rc = EXIT_FAILURE;
	size_t policy = 0;

	/*
	 *  Wait for all children to start before
	 *  ramping up the scheduler priority
	 */
	while (softlockup_start && stress_continue(args)) {
		shim_usleep(100000);
	}

	/*
	 * We run the stressor as a child so that
	 * if we hit the hard time limits the child is
	 * terminated with a SIGKILL and we can
	 * catch that with the parent
	 */
	rlim.rlim_cur = timeout;
	rlim.rlim_max = timeout;
	(void)setrlimit(RLIMIT_CPU, &rlim);

#if defined(RLIMIT_RTTIME)
	rlim.rlim_cur = 1000000 * timeout;
	rlim.rlim_max = 1000000 * timeout;
	(void)setrlimit(RLIMIT_RTTIME, &rlim);
#endif
	if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
		goto tidy;

	ret = sigsetjmp(jmp_env, 1);
	if (ret)
		goto tidy_ok;

	drop_niceness();

	policy = 0;
	do {
		uint8_t i, n = 30 + (stress_mwc8() & 0x3f);

		/*
		 *  Note: Re-setting the scheduler policy on Linux
		 *  puts the runnable process always onto the front
		 *  of the scheduling list.
		 */
		param->sched_priority = policies[policy].max_prio;
		ret = sched_setscheduler(mypid, policies[policy].policy, param);
		if (ret < 0) {
			if (errno != EPERM) {
				pr_fail("%s: sched_setscheduler "
					"failed: errno=%d (%s) "
					"for scheduler policy %s\n",
					args->name, errno, strerror(errno),
					policies[policy].name);
			}
		}
		drop_niceness();
		for (i = 0; i < n; i++)
			stress_softlockup_loop(loop_count);
		policy++;
		if (policy >= SIZEOF_ARRAY(policies))
			policy = 0;
		stress_bogo_inc(args);

		/* Ensure we NEVER spin forever */
		if ((stress_time_now() - start) > (double)timeout)
			break;
	} while (stress_continue(args));
tidy_ok:
	rc = EXIT_SUCCESS;
tidy:
	_exit(rc);
}

static int stress_softlockup(const stress_args_t *args)
{
	size_t policy = 0;
	int max_prio = 0, parent_cpu;
	bool good_policy = false;
	const bool first_instance = (args->instance == 0);
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	uint32_t i;
	struct sched_param param;
	NOCLOBBER uint64_t timeout;
	const double start = stress_time_now();
	pid_t *pids;
	int rc = EXIT_SUCCESS;
	uint64_t loop_count;

	softlockup_start = false;
	timeout = g_opt_timeout;
	(void)shim_memset(&param, 0, sizeof(param));

	loop_count = stress_softlockup_loop_count();

	pids = malloc(sizeof(*pids) * (size_t)cpus_online);
	if (!pids) {
		pr_inf_skip("%s: cannot allocate %" PRIu32 " pids, skipping stressor\n",
			args->name, cpus_online);
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < cpus_online; i++)
		pids[i] = -1;

	if (SIZEOF_ARRAY(policies) == (0)) {
		if (first_instance) {
			pr_inf_skip("%s: no scheduling policies "
					"available, skipping stressor\n",
					args->name);
		}
		free(pids);
		return EXIT_NOT_IMPLEMENTED;
	}

	/* Get the max priorities for each sched policy */
	for (policy = 0; policy < SIZEOF_ARRAY(policies); policy++) {
		const int prio = sched_get_priority_max(policies[policy].policy);

		policies[policy].max_prio = prio;
		good_policy |= (prio >= 0);

		if (max_prio < prio)
			max_prio = prio;
	}

	/*
	 *  We may have a kernel that does not support these sched
	 *  policies, so check for this
	 */
	if (!good_policy) {
		if (first_instance) {
			pr_inf_skip("%s: cannot get valid maximum priorities for the "
				"scheduling policies, skipping test\n",
					args->name);
		}
		free(pids);
		return EXIT_NOT_IMPLEMENTED;
	}

	if ((max_prio < 1) && (args->instance == 0)) {
		pr_inf("%s: running with a low maximum priority of %d\n",
			args->name, max_prio);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < cpus_online; i++) {
again:
		parent_cpu = stress_get_cpu();
		pids[i] = fork();
		if (pids[i] < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_inf("%s: cannot fork, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto finish;
		} else if (pids[i] == 0) {
			(void)stress_change_cpu(args, parent_cpu);
			stress_softlockup_child(args, &param, start, timeout, loop_count);
		}
	}
	param.sched_priority = policies[0].max_prio;
	(void)sched_setscheduler(args->pid, policies[0].policy, &param);

	softlockup_start = true;

	(void)pause();
	rc = stress_kill_and_wait_many(args, pids, (size_t)cpus_online, SIGALRM, false);
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(pids);

	return rc;
}

stressor_info_t stress_softlockup_info = {
	.stressor = stress_softlockup,
	.supported = stress_softlockup_supported,
	.class = CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_softlockup_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sched_get_priority_min() or sched_setscheduler()"
};
#endif
