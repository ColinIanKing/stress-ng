/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#if defined(HAVE_SIGALTSTACK)

static void *stack;
static const size_t stack_sz = MINSIGSTKSZ;
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
	uint8_t data[MINSIGSTKSZ * 2];

	(void)signum;
	(void)munmap(stack, stack_sz);
	(void)memset(data, 0, sizeof(data));

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

	stack = mmap(NULL, stack_sz, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED) {
		pr_err("%s: cannot mmap signal handler stack, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

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
				if (WTERMSIG(status) == SIGSEGV) {
					inc_counter(args);
					continue;
				}
			}
		} else if (pid == 0) {
			uint32_t rnd;
			int ret;

			if (sigsetjmp(jmpbuf, 1) != 0) {
				/*
				 *  We land here if we get a segfault
				 *  but not a segfault in the sighandler
				 */
				if (!keep_stressing())
					_exit(0);
			}

			if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
				return EXIT_FAILURE;
			if (stress_sigaltstack(stack, stack_sz) < 0)
				return EXIT_FAILURE;

			/* Child */
			stress_mwc_reseed();
			rnd = stress_mwc32() % 7;

			stress_set_oom_adjustment(args->name, true);
			stress_process_dumpable(false);

			switch (rnd) {
#if defined(HAVE_MPROTECT)
			case 1:
				ret = mprotect(stack, stack_sz, PROT_NONE);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing())
					break;
				CASE_FALLTHROUGH;
			case 2:
				ret = mprotect(stack, stack_sz, PROT_READ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing())
					break;
				CASE_FALLTHROUGH;
			case 3:
				ret = mprotect(stack, stack_sz, PROT_EXEC);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing())
					break;
				CASE_FALLTHROUGH;
#endif
			case 4:
				ret = stress_sigaltstack(NULL, SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing())
					break;
				CASE_FALLTHROUGH;
			case 5:
				ret = stress_sigaltstack(stress_segv_handler, SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
				if (!keep_stressing())
					break;
				CASE_FALLTHROUGH;
			case 6:
				stress_bad_altstack_force_fault(NULL);
				CASE_FALLTHROUGH;
			default:
			case 0:
				(void)munmap(stack, stack_sz);
				stress_bad_altstack_force_fault(NULL);
				break;
			}

			/* No luck, well that's unexpected.. */
			_exit(EXIT_FAILURE);
		}
	} while (keep_stressing());

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
