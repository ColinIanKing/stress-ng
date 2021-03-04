/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
     defined(PR_GET_UNALIGN)) ||		\
     defined(PR_GET_SPECULATION_CTRL) || 	\
     defined(PR_SET_SPECULATION_CTRL) || 	\
     defined(PR_SVE_GET_VL) ||			\
     defined(PR_SVE_SET_VL) ||			\
     defined(PR_GET_TAGGED_ADDR_CTRL) ||	\
     defined(PR_SET_TAGGED_ADDR_CTRL) ||	\
     defined(PR_GET_IO_FLUSHER) ||		\
     defined(PR_SET_IO_FLUSHER) ||		\
     defined(PR_PAC_RESET_KEYS)

#if defined(PR_SET_MM) &&		\
    defined(PR_SET_MM_AUXV)

/*
 *  getauxv_addr()
 *	fine the address of the auxv vector. This
 *	is located at the end of the environment.
 *	Returns NULL if not found.
 */
static inline void *getauxv_addr(void)
{
	char **env = environ;

	while (env && *env++)
		;

	return (void *)env;
}
#endif

/*
 *  stress_arch_prctl()
 *	exercise arch_prctl(), currently just
 *	x86-64 for now.
 */
static inline void stress_arch_prctl(void)
{
#if defined(HAVE_ASM_PRCTL_H) &&		\
    defined(HAVE_SYS_PRCTL_H) &&		\
    defined(ARCH_GET_CPUID) &&			\
    (defined(__x86_64__) || defined(__x86_64))
	{
		int ret;

		/* GET_CPUID setting, 2nd arg is ignored */
		ret = shim_arch_prctl(ARCH_GET_CPUID, 0);
#if defined(ARCH_SET_CPUID)
		if (ret >= 0)
			ret = shim_arch_prctl(ARCH_SET_CPUID, (unsigned long)ret);
		(void)ret;
#endif
	}
#endif

#if defined(HAVE_ASM_PRCTL_H) &&		\
    defined(HAVE_SYS_PRCTL_H) &&		\
    defined(ARCH_GET_FS) &&			\
    (defined(__x86_64__) || defined(__x86_64))
	{
		int ret;
		unsigned long fs;

		ret = shim_arch_prctl(ARCH_GET_FS, (unsigned long)&fs);
#if defined(ARCH_SET_FS)
		if (ret == 0)
			ret = shim_arch_prctl(ARCH_SET_FS, fs);
		(void)ret;
#endif
	}
#endif

#if defined(HAVE_ASM_PRCTL_H) &&		\
    defined(HAVE_SYS_PRCTL_H) &&		\
    defined(ARCH_GET_GS) &&			\
    (defined(__x86_64__) || defined(__x86_64))
	{
		int ret;
		unsigned long gs;

		ret = shim_arch_prctl(ARCH_GET_GS, (unsigned long)&gs);
#if defined(ARCH_SET_GS)
		if (ret == 0)
			ret = shim_arch_prctl(ARCH_SET_GS, gs);
		(void)ret;
#endif
	}
#endif
}

static int stress_prctl_child(const stress_args_t *args, const pid_t mypid)
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
		int mode;

		mode = prctl(PR_GET_FP_MODE);
		(void)mode;

#if defined(PR_SET_FP_MODE)
		if (mode >= 0) {
			ret = prctl(PR_SET_FP_MODE, mode);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_SVE_GET_VL)
	/* Get vector length */
	{
		int vl;

		vl = prctl(PR_SVE_GET_VL);
		(void)vl;

#if defined(PR_SVE_SET_VL)
		if (vl >= 0) {
			ret = prctl(PR_SVE_SET_VL, vl);
			(void)ret;
		}
#endif
	}
#endif

#if defined(PR_GET_TAGGED_ADDR_CTRL)
	{
		int ctrl;

		/* exercise invalid args */
		ctrl = prctl(PR_GET_TAGGED_ADDR_CTRL, ~0, ~0, ~0, ~0);
		(void)ctrl;
		ctrl = prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);
		(void)ctrl;

#if defined(PR_SET_TAGGED_ADDR_CTRL)
		if (ctrl >= 0) {
			/* exercise invalid args */
			ret = prctl(PR_SET_TAGGED_ADDR_CTRL, ~0, ~0, ~0, ~0);
			(void)ret;
			ret = prctl(PR_SET_TAGGED_ADDR_CTRL, ctrl, 0, 0, 0);
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
	/* exercise invalid args */
	ret = prctl(PR_MCE_KILL_GET, ~0, ~0, ~0, ~0);
	(void)ret;
	/* now exercise what is expected */
	ret = prctl(PR_MCE_KILL_GET, 0, 0, 0, 0);
	(void)ret;
#endif

#if defined(PR_MCE_KILL)
	/* exercise invalid args */
	ret = prctl(PR_MCE_KILL, PR_MCE_KILL_CLEAR, ~0, ~0, ~0);
	(void)ret;
	ret = prctl(PR_MCE_KILL, PR_MCE_KILL_SET, ~0, ~0, ~0);
	(void)ret;
	ret = prctl(PR_MCE_KILL, ~0, ~0, ~0, ~0);
	(void)ret;
	/* now exercise what is expected */
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
		const intptr_t mask = ~(args->page_size - 1);
		intptr_t addr;

		(void)stress_text_addr(&start, &end);

		addr = ((intptr_t)start) & mask;
		ret = prctl(PR_SET_MM, PR_SET_MM_START_CODE, addr, 0, 0);
		(void)ret;

		addr = ((intptr_t)end) & mask;
		ret = prctl(PR_SET_MM, PR_SET_MM_END_CODE, addr, 0, 0);
		(void)ret;
	}
#endif

#if defined(PR_SET_MM) &&		\
    defined(PR_SET_MM_ENV_START)
	{
		const intptr_t mask = ~(args->page_size - 1);
		const intptr_t addr = ((intptr_t)environ) & mask;

		ret = prctl(PR_SET_MM, PR_SET_MM_ENV_START, addr, 0, 0);
		(void)ret;
	}
#endif

#if defined(PR_SET_MM) &&		\
    defined(PR_SET_MM_AUXV)
	{
		void *auxv = getauxv_addr();

		if (auxv) {
			ret = prctl(PR_SET_MM, PR_SET_MM_AUXV, auxv, 0, 0);
			(void)ret;
		}
	}
#endif

#if defined(PR_MPX_ENABLE_MANAGEMENT)
	/* no longer implemented, use invalid args to force -EINVAL */
	ret = prctl(PR_MPX_ENABLE_MANAGEMENT, ~0, ~0, ~0, ~0);
	(void)ret;
#endif

#if defined(PR_MPX_DISABLE_MANAGEMENT)
	/* no longer implemented, use invalid args to force -EINVAL */
	ret = prctl(PR_MPX_DISABLE_MANAGEMENT, ~0, ~0, ~0, ~0);
	(void)ret;
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
			/* exercise invalid args */
			ret = prctl(PR_SET_NO_NEW_PRIVS, ret, ~0, ~0, ~0);
			(void)ret;
			/* now exercise what is expected */
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
			/* Exercise invalid signal */
			ret = prctl(PR_SET_PDEATHSIG, 0x10000);
			(void)ret;
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
			/* exercise invalid args */
			ret = prctl(PR_SET_THP_DISABLE, 0, 0, ~0, ~0);
			(void)ret;
			/* now exercise what is expected */
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
		int slack;

		slack = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
		(void)slack;

#if defined(PR_SET_TIMERSLACK)
		if (slack >= 0) {
			/* Zero timer slack will set to default timer slack */
			ret = prctl(PR_SET_TIMERSLACK, 0, 0, 0, 0);
			(void)ret;
			/* And restore back to original setting */
			ret = prctl(PR_SET_TIMERSLACK, slack, 0, 0, 0);
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

#if defined(PR_GET_SPECULATION_CTRL)
	{
		/* exercise invalid args */
		ret = prctl(PR_GET_SPECULATION_CTRL, ~0, ~0, ~0, ~0);
		(void)ret;
#if defined(PR_SPEC_STORE_BYPASS)
		ret = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, 0, 0, 0);
		(void)ret;
#endif
#if defined(PR_SPEC_INDIRECT_BRANCH)
		ret = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, 0, 0, 0);
		(void)ret;
#endif
	}
#endif

#if defined(PR_SET_SPECULATION_CTRL)
	{
		/* exercise invalid args */
		ret = prctl(PR_SET_SPECULATION_CTRL, ~0, ~0, ~0, ~0);
		(void)ret;
	}
#endif

#if defined(PR_GET_IO_FLUSHER)
	{
		ret = prctl(PR_GET_IO_FLUSHER, 0, 0, 0, 0);
		(void)ret;

#if defined(PR_SET_IO_FLUSHER)
		ret = prctl(PR_SET_IO_FLUSHER, ret, 0, 0, 0);
		(void)ret;
#endif
	}
#endif

#if defined(PR_PAC_RESET_KEYS)
	{
		/* exercise invalid args */
		ret = prctl(PR_PAC_RESET_KEYS, ~0, ~0, ~0, ~0);
		(void)ret;
	}
#endif
	stress_arch_prctl();

	/* exercise bad ptrcl command */
	{
		ret = prctl(-1, ~0, ~0, ~0, ~0);
		(void)ret;
		ret = prctl(0xf00000, ~0, ~0, ~0, ~0);
		(void)ret;
	}

	(void)ret;

	return 0;
}

/*
 *  stress_prctl()
 *	stress seccomp
 */
static int stress_prctl(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

		pid = fork();
		if (pid == -1) {
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (pid == 0) {
			int rc;
			pid_t mypid = getpid();

			(void)sched_settings_apply(true);

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
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

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
