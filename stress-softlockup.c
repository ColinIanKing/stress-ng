/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__)

typedef struct {
	int	policy;
	int	max_prio;
	char	*name;
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
static void MLOCKED stress_rlimit_handler(int dummy)
{
	(void)dummy;

	g_keep_stressing_flag = 1;
	siglongjmp(jmp_env, 1);
}

/*
 *  stress_softlockup_supported()
 *      check if we can run this as root
 */
int stress_softlockup_supported(void)
{
        if (geteuid() != 0) {
		pr_inf("softlockup stressor needs to be run as root to "
			"set SCHED_RR or SCHED_FIFO priorities, "
			"skipping this stressor\n");
                return -1;
        }
        return 0;
}

int stress_softlockup(const args_t *args)
{
	size_t policy = 0;
	int max_prio = 0;
	bool good_policy = false;
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	struct sigaction old_action_xcpu;
	struct sched_param param = { 0 };
	struct rlimit rlim;
	pid_t pid;
	uint64_t timeout = g_opt_timeout;

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

	if (args->num_instances < cpus_online) {
		pr_inf("%s: for best reslults, run with at least %d instances "
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
		double start = time_now();
		int ret;

		/*
		 * We run the stressor as a child so that
		 * if we the hard time timits the child is
		 * terminated with a SIGKILL and we can
		 * catch that with the parent
		 */
		rlim.rlim_cur = timeout;
		rlim.rlim_max = timeout;
		(void)setrlimit(RLIMIT_CPU, &rlim);

		rlim.rlim_cur = 1000000 * timeout;
		rlim.rlim_max = 1000000 * timeout;
		(void)setrlimit(RLIMIT_RTTIME, &rlim);

		if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
			_exit(EXIT_FAILURE);

		ret = sigsetjmp(jmp_env, 1);
		if (ret)
			_exit(EXIT_SUCCESS);

		policy = 0;
		do {
			int ret = 0;

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
				_exit(EXIT_SUCCESS);
		} while (keep_stressing());

		_exit(EXIT_SUCCESS);
	} else {
		int status;

		param.sched_priority = policies[0].max_prio;
		(void)sched_setscheduler(args->pid, policies[0].policy, &param);

		pause();
		kill(pid, SIGKILL);

		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
#else
int stress_softlockup(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif

