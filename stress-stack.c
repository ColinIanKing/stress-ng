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

static sigjmp_buf jmp_env;

static const help_t help[] = {
	{ NULL,	"stack N",	"start N workers generating stack overflows" },
	{ NULL,	"stack-ops N",	"stop after N bogo stack overflows" },
	{ NULL,	"stack-fill",	"fill stack, touches all new pages " },
	{ NULL,	NULL,		NULL }
};

static int stress_set_stack_fill(const char *opt)
{
	bool stack_fill = true;

	(void)opt;
	return set_setting("stack-fill", TYPE_ID_BOOL, &stack_fill);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_stack_fill,	stress_set_stack_fill },
	{ 0,			NULL }
};

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_stack_alloc()
 *	eat up stack. The default is to eat up lots of pages
 *	but only have 25% of the pages actually in memory
 *	so we a large stack with lots of pages not physically
 *	resident.
 */
static void stress_stack_alloc(const args_t *args, const bool stack_fill)
{
	const size_t sz = 256 * KB;
	const size_t page_size4 = (args->page_size << 2);
	char data[sz];

	if (stack_fill) {
		(void)memset(data, 0, sz);
	} else {
		register size_t i;

		/* Touch 25% of the pages */
		for (i = 0; i < sz; i += page_size4)
			data[i] = 0;
	}

	inc_counter(args);

	if (keep_stressing())
		stress_stack_alloc(args, stack_fill);
}


/*
 *  stress_stack
 *	stress by forcing stack overflows
 */
static int stress_stack(const args_t *args)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	pid_t pid;
	bool stack_fill = false;

	(void)get_setting("stack-fill", &stack_fill);

	/*
	 *  We need to create an alternative signal
	 *  stack so when a segfault occurs we use
	 *  this already allocated signal stack rather
	 *  than try to push onto an already overflowed
	 *  stack
	 */
	(void)memset(stack, 0, sizeof(stack));
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
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
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
		char *start_ptr = shim_sbrk(0);

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		if (start_ptr == (void *) -1) {
			pr_err("%s: sbrk(0) failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			_exit(EXIT_FAILURE);
		}

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		for (;;) {
			struct sigaction new_action;
			int ret;

			if (!keep_stressing())
				break;

			(void)memset(&new_action, 0, sizeof new_action);
			new_action.sa_handler = stress_segvhandler;
			(void)sigemptyset(&new_action.sa_mask);
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
				stress_stack_alloc(args, stack_fill);
			}
		}
		_exit(0);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_stack_info = {
	.stressor = stress_stack,
	.class = CLASS_VM | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
