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
	{ NULL,	"procfs N",	"start N workers reading portions of /proc" },
	{ NULL,	"procfs-ops N",	"stop procfs workers after N bogo read operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_PRCTL_H) &&		\
    defined(HAVE_PRCTL) &&			\
    (defined(PR_CAP_AMBIENT) ||			\
     defined(PR_CAPBSET_READ) ||		\
     defined(PR_CAPBSET_DROP) ||		\
     defined(PR_SET_CHILD_SUBREAPER) ||		\
     defined(PR_GET_CHILD_SUBREAPER) ||		\
     defined(PR_SET_DUMPABLE) ||		\
     defined(PR_GET_DUMPABLE) ||		\
     defined(PR_SET_ENDIAN) ||			\
     defined(PR_GET_ENDIAN) ||			\
     defined(PR_SET_FP_MODE) ||			\
     defined(PR_GET_FP_MODE) ||			\
     defined(PR_SET_FPEMU) ||			\
     defined(PR_GET_FPEMU) ||			\
     defined(PR_SET_FPEXC) ||			\
     defined(PR_GET_FPEXC) ||			\
     defined(PR_SET_KEEPCAPS) ||		\
     defined(PR_GET_KEEPCAPS) ||		\
     defined(PR_MCE_KILL) ||			\
     defined(PR_MCE_KILL_GET) ||		\
     defined(PR_SET_MM) ||			\
     defined(PR_MPX_ENABLE_MANAGEMENT) ||	\
     defined(PR_MPX_DISABLE_MANAGEMENT) ||	\
     defined(PR_SET_NAME) ||			\
     defined(PR_GET_NAME) ||			\
     defined(PR_SET_NO_NEW_PRIVS) ||		\
     defined(PR_GET_NO_NEW_PRIVS) ||		\
     defined(PR_SET_PDEATHSIG) ||		\
     defined(PR_GET_PDEATHSIG) ||		\
     defined(PR_SET_PTRACER) ||			\
     defined(PR_SET_SECCOMP) ||			\
     defined(PR_GET_SECCOMP) ||			\
     defined(PR_SET_SECUREBITS) ||		\
     defined(PR_GET_SECUREBITS) ||		\
     defined(PR_SET_THP_DISABLE) ||		\
     defined(PR_TASK_PERF_EVENTS_DISABLE) ||	\
     defined(PR_TASK_PERF_EVENTS_ENABLE) ||	\
     defined(PR_GET_THP_DISABLE) ||		\
     defined(PR_GET_TID_ADDRESS) ||		\
     defined(PR_SET_TIMERSLACK) ||		\
     defined(PR_GET_TIMERSLACK) ||		\
     defined(PR_SET_TIMING) ||			\
     defined(PR_GET_TIMING) ||			\
     defined(PR_SET_TSC) ||			\
     defined(PR_GET_TSC) ||			\
     defined(PR_SET_UNALIGN) ||			\
     defined(PR_GET_UNALIGN))

static int stress_prctl_child(const args_t *args, const pid_t mypid)
{
	int ret;

	(void)args;
	(void)mypid;

#if defined(PR_CAP_AMBIENT)
	/* skip for now */
#endif
#if defined(PR_CAPBSET_READ) && defined(CAP_CHOWN)
	ret = prctl(PR_CAPBSET_READ, CAP_CHOWN);
	(void)ret;
#endif

#if defined(PR_CAPBSET_DROP) && defined(CAP_CHOWN)
	ret = prctl(PR_CAPBSET_DROP, CAP_CHOWN);
	(void)ret;
#endif

#if defined(PR_GET_CHILD_SUBREAPER)
	{
		int reaper;

		ret = prctl(PR_GET_CHILD_SUBREAPER, &reaper);
		(void)ret;

#if defined(PR_SET_CHILD_SUBREAPER)
		if (ret == 0) {
			ret = prctl(PR_SET_CHILD_SUBREAPER, reaper);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_DUMPABLE)
	{
		ret = prctl(PR_GET_DUMPABLE);
		(void)ret;

#if defined(PR_SET_DUMPABLE)
		if (ret >= 0) {
			ret = prctl(PR_SET_DUMPABLE, ret);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_ENDIAN)
	/* PowerPC only, but try it on all arches */
	{
		int endian;

		ret = prctl(PR_GET_ENDIAN, &endian);
		(void)ret;

#if defined(PR_SET_ENDIAN)
		if (ret == 0) {
			ret = prctl(PR_SET_ENDIAN, endian);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_FP_MODE)
	/* MIPS only, but try it on all arches */
	{
		unsigned int mode;

		ret = prctl(PR_GET_FP_MODE, &mode);
		(void)ret;

#if defined(PR_SET_FP_MODE)
		if (ret == 0) {
			ret = prctl(PR_SET_FP_MODE, mode);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_FPEMU)
	/* ia64 only, but try it on all arches */
	{
		int control;

		ret = prctl(PR_GET_FPEMU, &control);
		(void)ret;

#if defined(PR_SET_FPEMU)
		if (ret == 0) {
			ret = prctl(PR_SET_FPEMU, control);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_FPEXC)
	/* PowerPC only, but try it on all arches */
	{
		int mode;

		ret = prctl(PR_GET_FPEXC, &mode);
		(void)ret;

#if defined(PR_SET_FPEXC)
		if (ret == 0) {
			ret = prctl(PR_SET_FPEXC, mode);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_KEEPCAPS)
	{
		int flag;

		ret = prctl(PR_GET_KEEPCAPS, &flag);
		(void)ret;

#if defined(PR_SET_KEEPCAPS)
		if (ret == 0) {
			ret = prctl(PR_SET_KEEPCAPS, flag);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_MCE_KILL_GET)
	ret = prctl(PR_MCE_KILL_GET, 0, 0, 0, 0);
	(void)ret;
#endif

#if defined(PR_MCE_KILL)
	ret = prctl(PR_MCE_KILL, PR_MCE_KILL_CLEAR, 0, 0, 0);
	(void)ret;
#endif

#if defined(PR_SET_MM) &&	\
    defined(PR_SET_MM_BRK)
	ret = prctl(PR_SET_MM, PR_SET_MM_BRK, sbrk(0), 0, 0);
	(void)ret;
#endif

#if defined(PR_SET_MM) &&		\
    defined(PR_SET_MM_START_CODE) &&	\
    defined(PR_SET_MM_END_CODE)
	{
		char *start, *end;
		const ptrdiff_t mask = ~(args->page_size - 1);
		ptrdiff_t addr;

		(void)stress_text_addr(&start, &end);

		addr = ((ptrdiff_t)start) & mask;
		ret = prctl(PR_SET_MM, PR_SET_MM_START_CODE, addr, 0, 0);
		(void)ret;

		addr = ((ptrdiff_t)end) & mask;
		ret = prctl(PR_SET_MM, PR_SET_MM_END_CODE, addr, 0, 0);
		(void)ret;
	}
#endif

#if defined(PR_SET_MM) &&		\
    defined(PR_SET_MM_ENV_START)
	{
		const ptrdiff_t mask = ~(args->page_size - 1);
		const ptrdiff_t addr = ((ptrdiff_t)environ) & mask;

		ret = prctl(PR_SET_MM, PR_SET_MM_ENV_START, addr, 0, 0);
		(void)ret;
	}
#endif

#if defined(PR_MPX_ENABLE_MANAGEMENT)
	/* skip this for now */
#endif

#if defined(PR_MPX_DISABLE_MANAGEMENT)
	/* skip this for now */
#endif

#if defined(PR_GET_NAME)
	{
		char name[17];

		(void)memset(name, 0, sizeof name);

		ret = prctl(PR_GET_NAME, name);
		(void)ret;

#if defined(PR_SET_NAME)
		if (ret == 0) {
			ret = prctl(PR_SET_NAME, name);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_NO_NEW_PRIVS)
	{
		ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_NO_NEW_PRIVS)
		if (ret >= 0) {
			ret = prctl(PR_SET_NO_NEW_PRIVS, ret, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_PDEATHSIG)
	{
		int sig;

		ret = prctl(PR_GET_PDEATHSIG, &sig);
		(void)ret;

#if defined(PR_SET_PDEATHSIG)
		if (ret == 0) {
			ret = prctl(PR_SET_PDEATHSIG, sig);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_SET_PTRACER)
	{
		ret = prctl(PR_SET_PTRACER, mypid, 0, 0, 0);
		(void)ret;
#if defined(PR_SET_PTRACER_ANY)
		ret = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
		(void)ret;
#endif
		ret = prctl(PR_SET_PTRACER, 0, 0, 0, 0);
		(void)ret;
	}
#endif

#if defined(PR_GET_SECCOMP)
	{
		ret = prctl(PR_GET_SECCOMP);
		(void)ret;

#if defined(PR_SET_SECCOMP)
	/* skip this for the moment */
#endif
	}
#endif

#if defined(PR_GET_SECUREBITS)
	{
		ret = prctl(PR_GET_SECUREBITS, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_SECUREBITS)
		if (ret >= 0) {
			ret = prctl(PR_SET_SECUREBITS, ret, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_THP_DISABLE)
	{
		ret = prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_THP_DISABLE)
		if (ret >= 0) {
			ret = prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_TASK_PERF_EVENTS_DISABLE)
	ret = prctl(PR_TASK_PERF_EVENTS_DISABLE);
	(void)ret;
#endif

#if defined(PR_TASK_PERF_EVENTS_ENABLE)
	ret = prctl(PR_TASK_PERF_EVENTS_ENABLE);
	(void)ret;
#endif

#if defined(PR_GET_TID_ADDRESS)
	{
		uint64_t val;

		ret = prctl(PR_GET_TID_ADDRESS, &val);
		(void)ret;
	}
#endif

#if defined(PR_GET_TIMERSLACK)
	{
		ret = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_TIMERSLACK)
		if (ret >= 0) {
			ret = prctl(PR_SET_TIMERSLACK, ret, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_TIMING)
	{
		ret = prctl(PR_GET_TIMING, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_TIMING)
		if (ret >= 0) {
			ret = prctl(PR_SET_TIMING, ret, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_TSC)
	{
		/* x86 only, but try it on all arches */
		int state;

		ret = prctl(PR_GET_TSC, &state, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_TSC)
		if (ret == 0) {
			ret = prctl(PR_SET_TSC, state, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_UNALIGN)
	{
		/* ia64, parisc, powerpc, alpha, sh, tile, but try it on all arches */
		unsigned int control;

		ret = prctl(PR_GET_UNALIGN, &control, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_UNALIGN)
		if (ret == 0) {
			ret = prctl(PR_SET_UNALIGN, control, 0, 0, 0);
			(void)ret;
		}
#endif
	}
#endif
	(void)ret;

	return 0;
}

/*
 *  stress_prctl()
 *	stress seccomp
 */
static int stress_prctl(const args_t *args)
{
	do {
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			pr_fail_err("fork");
			break;
		}
		if (pid == 0) {
			int rc;
			pid_t mypid = getpid();

			rc = stress_prctl_child(args, mypid);
			_exit(rc);
		}
		if (pid > 0) {
			int status;

			/* Wait for child to exit or get killed by seccomp */
			if (shim_waitpid(pid, &status, 0) < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid failed, errno = %d (%s)\n",
						args->name, errno, strerror(errno));
			} else {
				/* Did the child hit a weird error? */
				if (WIFEXITED(status) &&
				    (WEXITSTATUS(status) != EXIT_SUCCESS)) {
					pr_fail("%s: aborting because of unexpected "
						"failure in child process\n", args->name);
					return EXIT_FAILURE;
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_prctl_info = {
	.stressor = stress_prctl,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_prctl_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
