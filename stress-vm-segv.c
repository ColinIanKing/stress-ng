// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"vm-segv N",	 "start N workers that unmap their address space" },
	{ NULL,	"vm-segv-ops N", "stop after N vm-segv unmap'd SEGV faults" },
	{ NULL,	NULL,		 NULL }
};

#define MSG_CHILD_STARTED	(0x271efb9c)

static NOINLINE void vm_unmap_child(const size_t page_size)
{
	size_t len = ~(size_t)0;
	void *addr = stress_align_address((void *)vm_unmap_child, page_size);

	len = len ^ (len >> 1);
	while (len > page_size) {
		(void)munmap((void *)0, len - page_size);
		len >>= 1;
#if !defined(__DragonFly__)
		shim_clflush(addr);
#endif
		shim_flush_icache(addr, (void *)(((uint8_t *)addr) + 64));
	}
}

static NOINLINE void vm_unmap_self(const size_t page_size)
{
	void *addr = stress_align_address((void *)vm_unmap_self, page_size);

	(void)munmap(addr, page_size);
#if !defined(__DragonFly__)
	shim_clflush(addr);
#endif
	shim_flush_icache(addr, (void *)(((uint8_t *)addr) + 64));
}

/*
 *  stress_vm_segv()
 *	stress vm segv by unmapping child's address space
 *	and generating a segv on return because child has
 *	no address space on return.
 */
static int stress_vm_segv(const stress_args_t *args)
{
	bool test_valid = false;

	stress_set_oom_adjustment(args, true);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		int fd[2];

		if (pipe(fd) < 0) {
			pr_inf("%s: pipe failed, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			return EXIT_NO_RESOURCE;
		}

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (pid > 0) {
			int status, ret, msg;
			ssize_t rret;

			(void)close(fd[1]);

			/* Parent, wait for child */
			rret = read(fd[0], &msg, sizeof(msg));
			if (rret < (ssize_t)sizeof(msg))
				goto kill_child;

			if (msg == MSG_CHILD_STARTED) {
				test_valid = true;
			} else {
				pr_dbg("%s: did not get child start message 0x%x got 0x%x instead\n",
					args->name, MSG_CHILD_STARTED, msg);
				goto kill_child;
			}

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0)
				goto kill_child;
			if (WTERMSIG(status) == SIGSEGV)
				stress_bogo_inc(args);
kill_child:
			(void)close(fd[0]);
			stress_kill_and_wait(args, pid, SIGTERM, false);
		} else {
			/* Child */
			sigset_t set;
			ssize_t wret;
			const size_t page_size = args->page_size;
			const int msg = MSG_CHILD_STARTED;

			stress_set_oom_adjustment(args, true);
			stress_process_dumpable(false);
			(void)sched_settings_apply(true);

			(void)close(fd[0]);

			wret = write(fd[1], &msg, sizeof(msg));
			if (wret == (ssize_t)sizeof(msg)) {
				(void)sigemptyset(&set);
				(void)sigaddset(&set, SIGSEGV);
				(void)sigprocmask(SIG_BLOCK, &set, NULL);

				/*
				 *  Try to ummap the child's address space, should cause
				 *  a SIGSEGV at some point..
				 */
				vm_unmap_child(page_size);

				/*
				 *  That failed, so try unmapping this function
				 */
				vm_unmap_self(page_size);
			}
			/* No luck, well that's unexpected.. */
			(void)close(fd[1]);
			_exit(EXIT_FAILURE);
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (test_valid && (stress_bogo_get(args) == 0))
		pr_fail("%s: no SIGSEGV signals detected\n", args->name);

	return EXIT_SUCCESS;
}

stressor_info_t stress_vm_segv_info = {
	.stressor = stress_vm_segv,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
