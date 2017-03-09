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

static sigjmp_buf jmp_env;

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED stress_segvhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}


/*
 *  stress_stack
 *	stress by forcing stack overflows
 */
int stress_stack(const args_t *args)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	pid_t pid;

	/*
	 *  We need to create an alternative signal
	 *  stack so when a segfault occurs we use
	 *  this already allocated signal stack rather
	 *  than try to push onto an already overflowed
	 *  stack
	 */
	memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return EXIT_FAILURE;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n", args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		char *start_ptr = sbrk(0);

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		if (start_ptr == (void *) -1) {
			pr_err("%s: sbrk(0) failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		for (;;) {
			struct sigaction new_action;
			int ret;

			if (!keep_stressing())
				break;

			memset(&new_action, 0, sizeof new_action);
			new_action.sa_handler = stress_segvhandler;
			sigemptyset(&new_action.sa_mask);
			new_action.sa_flags = SA_ONSTACK;

			if (sigaction(SIGSEGV, &new_action, NULL) < 0) {
				pr_fail_err("sigaction");
				return EXIT_FAILURE;
			}
			if (sigaction(SIGBUS, &new_action, NULL) < 0) {
				pr_fail_err("sigaction");
				return EXIT_FAILURE;
			}
			ret = sigsetjmp(jmp_env, 1);
			/*
			 * We return here if we segfault, so
			 * first check if we need to terminate
			 */
			if (!keep_stressing())
				break;

			if (ret) {
				/* We end up here after handling the fault */
				inc_counter(args);
			} else {
				/* Expand the stack and cause a fault */
				char *last_ptr = 0;
				do {
					char *ptr = alloca(256 * KB);

					/*
					 * need this else gcc optimises out
					 * the alloca()
					 */
					if (g_opt_flags & OPT_STACK_FILL)
						memset(ptr, 0, 256 * KB);
					else
						*ptr = 0;

					/* Force gcc to actually do the alloca */
					uint64_put((uint64_t)(last_ptr - ptr));
					last_ptr = ptr;
				} while (g_keep_stressing_flag);
			}
		}
	}

	return EXIT_SUCCESS;
}
