/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"

#include <sched.h>

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

#if defined(HAVE_ASM_X86_REP_STOSB) &&	\
    !defined(__ILP32__)
#define HAVE_X86_REP_STOSB
static ALWAYS_INLINE inline void OPTIMIZE3 stress_softlockup_stosb(void *ptr, const uint32_t loops)
{
	register const void *p = ptr;
	register const uint32_t l = loops;

	__asm__ __volatile__(
		"mov $0xaa,%%al\n;"
		"mov %0,%%rdi\n;"
		"mov %1,%%ecx\n;"
		"rep stosb %%al,%%es:(%%rdi);\n"
		:
		: "r" (p),
		  "r" (l)
		: "ecx","rdi","al");
}

static uint8_t *softlockup_buffer;
#endif

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
	uint64_t n = 1024 * 64, i;

	do {
		double t, d;

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

#if defined(HAVE_X86_REP_STOSB)
static void OPTIMIZE3 stress_softlockup_rep_stosb(void)
{
	if (softlockup_buffer == MAP_FAILED)
		return;

	stress_softlockup_stosb(softlockup_buffer, MB);
}
#else
static void stress_softlockup_rep_stosb(void)
{
}
#endif

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void MLOCKED_TEXT NORETURN stress_rlimit_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
	siglongjmp(jmp_env, 1);
	stress_no_return();
}

/*
 *  drop_niceness();
 *	see how low we can go with niceness
 */
static void drop_niceness(void)
{
	int i;

	/*
	 *  Traditionally no more than -20, but see if we
	 *  can force it lower if we are originally running
	 *  at nice level 19
	 */
	errno = EPERM;
	for (i = -40; (i < 0) && errno; i++) {
		errno = 0;
		VOID_RET(int, shim_nice(i));
	}
}

static void stress_softlockup_child(
	stress_args_t *args,
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
		(void)shim_usleep(100000);
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
					"failed, errno=%d (%s) "
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

		stress_softlockup_rep_stosb();

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

static int stress_softlockup(stress_args_t *args)
{
	size_t policy = 0;
	int max_prio = 0, parent_cpu;
	bool good_policy = false;
	const bool first_instance = (stress_instance_zero(args));
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	uint32_t i;
	struct sched_param param;
	NOCLOBBER uint64_t timeout;
	const double start = stress_time_now();
	stress_pid_t *s_pids, *s_pids_head = NULL;
	int rc = EXIT_SUCCESS;
	uint64_t loop_count;

	softlockup_start = false;
	timeout = g_opt_timeout;
	(void)shim_memset(&param, 0, sizeof(param));

	loop_count = stress_softlockup_loop_count();

#if defined(HAVE_X86_REP_STOSB)
	softlockup_buffer = (uint8_t *)mmap(NULL, MB, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (softlockup_buffer != MAP_FAILED)
		stress_set_vma_anon_name(softlockup_buffer, MB, "x86-rep-stosb-data");
#endif

	s_pids = stress_sync_s_pids_mmap((size_t)cpus_online);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
			args->name, (size_t)cpus_online, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < cpus_online; i++)
		stress_sync_start_init(&s_pids[i]);

	if (SIZEOF_ARRAY(policies) == (0)) {
		if (first_instance) {
			pr_inf_skip("%s: no scheduling policies "
					"available, skipping stressor\n",
					args->name);
		}
		(void)stress_sync_s_pids_munmap(s_pids, (size_t)cpus_online);
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
				"scheduling policies, skipping stressor\n",
					args->name);
		}
		(void)stress_sync_s_pids_munmap(s_pids, (size_t)cpus_online);
		return EXIT_NOT_IMPLEMENTED;
	}

	if ((max_prio < 1) && (stress_instance_zero(args))) {
		pr_inf("%s: running with a low maximum priority of %d\n",
			args->name, max_prio);
	}

	for (i = 0; i < cpus_online; i++) {
again:
		parent_cpu = stress_get_cpu();
		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_inf("%s: cannot fork, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto finish;
		} else if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			(void)stress_change_cpu(args, parent_cpu);
			stress_softlockup_child(args, &param, start, timeout, loop_count);
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	param.sched_priority = policies[0].max_prio;
	(void)sched_setscheduler(args->pid, policies[0].policy, &param);

	softlockup_start = true;

	(void)shim_pause();
	rc = stress_kill_and_wait_many(args, s_pids, (size_t)cpus_online, SIGALRM, false);
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_X86_REP_STOSB)
	if (softlockup_buffer != MAP_FAILED)
		(void)munmap((void *)softlockup_buffer, MB);
#endif

	(void)stress_sync_s_pids_munmap(s_pids, (size_t)cpus_online);

	return rc;
}

const stressor_info_t stress_softlockup_info = {
	.stressor = stress_softlockup,
	.supported = stress_softlockup_supported,
	.classifier = CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_softlockup_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sched_get_priority_min() or sched_setscheduler()"
};
#endif
