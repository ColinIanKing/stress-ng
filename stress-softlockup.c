/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
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
static int stress_softlockup_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_SYS_NICE)) {
		pr_inf("softlockup stressor will be skipped, "
			"need to be running with CAP_SYS_NICE "
			"rights for this stressor\n");
		return -1;
	}
	return 0;
}

typedef struct {
	const int	policy;
	int		max_prio;
	const char	*name;
} policy_t;

static policy_t policies[] = {
#if defined(SCHED_FIFO)
	{ SCHED_FIFO, 0, "SCHED_FIFO"},
#endif
#if defined(SCHED_RR)
	{ SCHED_RR, 0, "SCHED_RR" },
#endif
};

static sigjmp_buf jmp_env;

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void MLOCKED_TEXT stress_rlimit_handler(int signum)
{
	(void)signum;

	g_keep_stressing_flag = 1;
	siglongjmp(jmp_env, 1);
}

static int stress_softlockup(const args_t *args)
{
	size_t policy = 0;
	int max_prio = 0;
	bool good_policy = false;
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	const uint32_t num_instances = args->num_instances;
	struct sigaction old_action_xcpu;
	struct sched_param param;
	struct rlimit rlim;
	pid_t pid;
	NOCLOBBER uint64_t timeout;
	const double start = time_now();

	timeout = g_opt_timeout;
	(void)memset(&param, 0, sizeof(param));

	if (!args->instance) {
		if (SIZEOF_ARRAY(policies) == 0) {
			pr_inf("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
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
		pr_inf("%s: cannot get valid maximum priorities for the "
			"scheduling policies, skipping test\n",
				args->name);
		return EXIT_NOT_IMPLEMENTED;
	}

	if (!args->instance) {
		if (max_prio < 1)
			pr_inf("%s: running with a low maximum priority of %d\n",
				args->name, max_prio);
	}

	if (num_instances < cpus_online) {
		pr_inf("%s: for best results, run with at least %d instances "
			"of this stressor\n", args->name, cpus_online);
	}

	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		timeout = 60;
		pr_inf("%s: timeout has not been set, forcing timeout to "
			"be %" PRIu64 " seconds\n", args->name, timeout);
	}

	pid = fork();
	if (pid < 0) {
		pr_inf("%s: cannot fork, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		const pid_t mypid = getpid();
#if defined(HAVE_ATOMIC)
		uint32_t count;
#endif
		int ret;
		int rc = EXIT_FAILURE;

#if defined(HAVE_ATOMIC)
		__sync_fetch_and_add(&g_shared->softlockup_count, 1);

		/*
		 * Wait until all instances have reached this point
		 */
		do {
			if ((time_now() - start) > (double)timeout)
				goto tidy_ok;
			(void)usleep(50000);
			__atomic_load(&g_shared->softlockup_count, &count, __ATOMIC_RELAXED);
		} while (keep_stressing() && count < num_instances);
#endif

		/*
		 * We run the stressor as a child so that
		 * if we the hard time timits the child is
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

		policy = 0;
		do {
			/*
			 *  Note: Re-setting the scheduler policy on Linux
			 *  puts the runnable process always onto the front
			 *  of the scheduling list.
			 */
			param.sched_priority = policies[policy].max_prio;
			ret = sched_setscheduler(mypid, policies[policy].policy, &param);
			if (ret < 0) {
				if (errno != EPERM) {
					pr_fail("%s: sched_setscheduler "
						"failed: errno=%d (%s) "
						"for scheduler policy %s\n",
						args->name, errno, strerror(errno),
						policies[policy].name);
				}
			}
			policy++;
			policy %= SIZEOF_ARRAY(policies);
			inc_counter(args);

			/* Ensure we NEVER spin forever */
			if ((time_now() - start) > (double)timeout)
				break;
		} while (keep_stressing());

tidy_ok:
		rc = EXIT_SUCCESS;
tidy:
		(void)fflush(stdout);
		_exit(rc);
	} else {
		int status;

		param.sched_priority = policies[0].max_prio;
		(void)sched_setscheduler(args->pid, policies[0].policy, &param);

		(void)pause();
		(void)kill(pid, SIGKILL);
#if defined(HAVE_ATOMIC)
		__sync_fetch_and_sub(&g_shared->softlockup_count, 1);
#endif

		(void)shim_waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_softlockup_info = {
	.stressor = stress_softlockup,
	.supported = stress_softlockup_supported,
	.class = CLASS_SCHEDULER,
	.help = help
};
#else
stressor_info_t stress_softlockup_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER,
	.help = help
};
#endif
