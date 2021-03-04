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

static const stress_help_t help[] =
{
	{ NULL,	"bad-altstack N",	"start N workers exercising bad signal stacks" },
	{ NULL,	"bad-altstack-ops N",	"stop after N bogo signal stack SIGSEGVs" },
	{ NULL, NULL,			NULL }
};

#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_GETAUXVAL) && \
    defined(AT_SYSINFO_EHDR)
#define HAVE_VDSO_VIA_GETAUXVAL	(1)
#endif

#if defined(HAVE_SIGALTSTACK)

static void *stack;
static void *zero_stack;
static sigjmp_buf jmpbuf;

static inline void stress_bad_altstack_force_fault(uint8_t *stack_start)
{
	volatile uint8_t *vol_stack = (volatile uint8_t *)stack_start;
	/* trigger segfault on stack */

	stress_uint8_put(*vol_stack);
	*vol_stack = 0;
	(void)*vol_stack;
}

static void MLOCKED_TEXT stress_segv_handler(int signum)
{
	uint8_t data[STRESS_MINSIGSTKSZ * 2];

	(void)signum;
	(void)munmap(stack, STRESS_MINSIGSTKSZ);
	(void)memset(data, 0xff, sizeof(data));
	stress_uint8_put(data[0]);

	if (zero_stack != MAP_FAILED)
		(void)munmap(zero_stack, STRESS_MINSIGSTKSZ);

	/*
	 *  If we've not got this far we've not
	 *  generated a fault inside the stack of the
	 *  signal handler, so jmp back and re-try
	 */
	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_bad_altstack()
 *	create bad alternative signal stacks and cause
 *	a SIGSEGV when handling SIGSEGVs. The kernel
 *	should kill these.
 */
static int stress_bad_altstack(const stress_args_t *args)
{
	stress_set_oom_adjustment(args->name, true);
#if defined(HAVE_VDSO_VIA_GETAUXVAL)
	void *vdso = (void *)getauxval(AT_SYSINFO_EHDR);
#endif
	int fd;

	stack = mmap(NULL, STRESS_MINSIGSTKSZ, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED) {
		pr_err("%s: cannot mmap signal handler stack, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	fd = open("/dev/zero", O_RDONLY);
	if (fd >= 0) {
		zero_stack = mmap(NULL, STRESS_MINSIGSTKSZ, PROT_READ,
			MAP_PRIVATE, fd, 0);
		(void)close(fd);
	} else {
		zero_stack = MAP_FAILED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

		(void)stress_mwc32();
again:
		if (!keep_stressing_flag())
			return EXIT_SUCCESS;
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == EINTR) || (errno == ENOMEM))
				goto again;
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (pid > 0) {
			int status, ret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			} else if (WIFSIGNALED(status)) {
				/* If we got killed by OOM killer, re-start */
				if (WTERMSIG(status) == SIGKILL) {
					if (g_opt_flags & OPT_FLAGS_OOMABLE) {
						stress_log_system_mem_info();
						pr_dbg("%s: assuming killed by OOM "
							"killer, bailing out "
							"(instance %d)\n",
							args->name, args->instance);
						_exit(0);
					} else {
						stress_log_system_mem_info();
						pr_dbg("%s: assuming killed by OOM "
							"killer, restarting again "
							"(instance %d)\n",
							args->name, args->instance);
						goto again;
					}
				}
				/* expected: child killed itself with SIGSEGV */
				if (WTERMSIG(status) == SIGSEGV)
					inc_counter(args);
			}
		} else if (pid == 0) {
			uint32_t rnd;
			int ret;
			stack_t ss, old_ss;

			if (sigsetjmp(jmpbuf, 1) != 0) {
				/*
				 *  We land here if we get a segfault
				 *  but not a segfault in the sighandler
				 */
				if (!keep_stressing(args))
					_exit(0);
			}

			/* Exercise fetch of old ss, return 0 */
			(void)sigaltstack(NULL, &old_ss);

			/* Exercise disable SS_DISABLE */
			ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
			ss.ss_size = STRESS_MINSIGSTKSZ;
			ss.ss_flags = SS_DISABLE;
			(void)sigaltstack(&ss, NULL);

			/* Exercise invalid flags */
			ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
			ss.ss_size = STRESS_MINSIGSTKSZ;
			ss.ss_flags = ~0;
			(void)sigaltstack(&ss, NULL);

			/* Exercise no-op, return 0 */
			(void)sigaltstack(NULL, NULL);

			/* Exercise less than minimum allowed stack size, ENOMEM */
			ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
			ss.ss_size = STRESS_MINSIGSTKSZ - 1;
			ss.ss_flags = 0;
			(void)sigaltstack(&ss, NULL);

			if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
				return EXIT_FAILURE;

			/* Set alternative stack for testing */
			if (stress_sigaltstack(stack, STRESS_MINSIGSTKSZ) < 0)
				return EXIT_FAILURE;

			/* Child */
			stress_mwc_reseed();
			rnd = stress_mwc32() % 9;

			stress_set_oom_adjustment(args->name, true);
			stress_process_dumpable(false);
			(void)sched_settings_apply(true);

			switch (rnd) {
#if defined(HAVE_MPROTECT)
			case 1:
				/* Illegal stack with no protection */
				ret = mprotect(stack, STRESS_MINSIGSTKSZ, PROT_NONE);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
				CASE_FALLTHROUGH;
			case 2:
				/* Illegal read-only stack */
				ret = mprotect(stack, STRESS_MINSIGSTKSZ, PROT_READ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
				CASE_FALLTHROUGH;
			case 3:
				/* Illegal exec-only stack */
				ret = mprotect(stack, STRESS_MINSIGSTKSZ, PROT_EXEC);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
				CASE_FALLTHROUGH;
#endif
			case 4:
				/* Illegal NULL stack */
				ret = stress_sigaltstack(NULL, STRESS_SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
				CASE_FALLTHROUGH;
			case 5:
				/* Illegal text segment stack */
				ret = stress_sigaltstack(stress_segv_handler, STRESS_SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
				CASE_FALLTHROUGH;
			case 6:
				/* Small stack */
				stress_bad_altstack_force_fault(NULL);
				CASE_FALLTHROUGH;
			case 7:
#if defined(HAVE_VDSO_VIA_GETAUXVAL)
				/* Illegal stack on VDSO, otherwises NULL stack */
				ret = stress_sigaltstack(vdso, STRESS_SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing(args))
					break;
#endif
				CASE_FALLTHROUGH;
			case 8:
				/* Illegal /dev/zero mapped stack */
				if (zero_stack != MAP_FAILED) {
					ret = stress_sigaltstack(zero_stack, STRESS_MINSIGSTKSZ);
					if (ret == 0)
						stress_bad_altstack_force_fault(zero_stack);
					if (!keep_stressing(args))
						break;
				}
				CASE_FALLTHROUGH;
			default:
			case 0:
				/* Illegal unmapped stack */
				(void)munmap(stack, STRESS_MINSIGSTKSZ);
				stress_bad_altstack_force_fault(NULL);
				break;
			}

			/* No luck, well that's unexpected.. */
			_exit(EXIT_FAILURE);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (zero_stack != MAP_FAILED)
		(void)munmap(zero_stack, STRESS_MINSIGSTKSZ);
	if (stack != MAP_FAILED)
		(void)munmap(stack, STRESS_MINSIGSTKSZ);

	return EXIT_SUCCESS;
}

stressor_info_t stress_bad_altstack_info = {
	.stressor = stress_bad_altstack,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_bad_altstack_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#endif
