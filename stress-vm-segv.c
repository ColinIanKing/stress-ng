/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-put.h"

static const stress_help_t help[] = {
	{ NULL,	"vm-segv N",	 "start N workers that unmap their address space" },
	{ NULL,	"vm-segv-ops N", "stop after N vm-segv unmap'd SEGV faults" },
	{ NULL,	NULL,		 NULL }
};

#define MSG_CHILD_STARTED	(0x271efb9c)

static NOINLINE void vm_unmap_child(const size_t page_size)
{
	size_t len = ~(size_t)0;
	uint8_t *addr = stress_align_address((void *)vm_unmap_child, page_size);

	len = len ^ (len >> 1);
	while (len > page_size) {
		(void)munmap((void *)stress_get_null(), len - page_size);
		len >>= 1;
#if !defined(__DragonFly__) &&	\
    !defined(__OpenBSD__)
		shim_clflush((void *)addr);
#endif
		shim_flush_icache(addr, (void *)(addr + 64));
	}
}

static NOINLINE void vm_unmap_self(const size_t page_size)
{
#if defined(__APPLE__)
	(void)page_size;

	/*
	 *  munmapping one's self seems to cause OS X child
	 *  processes to hang, so don't do it
	 */
#else
	uint8_t *addr = stress_align_address((void *)vm_unmap_self, page_size);

	(void)munmap((void *)addr, page_size);
	(void)munmap((void *)(addr - page_size), page_size);
#if !defined(__DragonFly__)
	shim_clflush(addr);
#endif
	shim_flush_icache(addr, (void *)(addr + 64));
#endif
}

static NOINLINE OPTIMIZE0 void vm_unmap_stack(const size_t page_size)
{
	uint32_t stackvar = 0;
	uint8_t *addr = stress_align_address((void *)&stackvar, page_size);

#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_READ)
	(void)mprotect((void *)addr, page_size, PROT_READ);
	(void)mprotect((void *)(addr - page_size), page_size, PROT_READ);
#endif
	(void)munmap((void *)addr, page_size);
	(void)munmap((void *)(addr - page_size), page_size);
}

/*
 *  stress_vm_segv()
 *	stress vm segv by unmapping child's address space
 *	and generating a segv on return because child has
 *	no address space on return.
 */
static int stress_vm_segv(stress_args_t *args)
{
	bool test_valid = false;

	stress_set_oom_adjustment(args, true);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		int fd[2];

		if (UNLIKELY(pipe(fd) < 0)) {
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
			if (UNLIKELY(!stress_continue(args))) {
				(void)close(fd[0]);
				(void)close(fd[1]);
				goto finish;
			}
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd[0]);
			(void)close(fd[1]);
			return EXIT_NO_RESOURCE;
		} else if (pid > 0) {
			int status, msg;
			ssize_t rret;

			(void)close(fd[1]);

			/* Parent, wait for child */
			rret = read(fd[0], &msg, sizeof(msg));
			if (UNLIKELY(rret < (ssize_t)sizeof(msg)))
				goto kill_child;

			if (msg == MSG_CHILD_STARTED) {
				test_valid = true;
			} else {
				pr_dbg("%s: did not get child start message 0x%x got 0x%x instead\n",
					args->name, MSG_CHILD_STARTED, msg);
				goto kill_child;
			}

			if (shim_waitpid(pid, &status, 0) < 0)
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

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_set_oom_adjustment(args, true);
			stress_process_dumpable(false);
			(void)sched_settings_apply(true);

			(void)close(fd[0]);

			wret = write(fd[1], &msg, sizeof(msg));
			if (LIKELY(wret == (ssize_t)sizeof(msg))) {
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

				/*
				 *  That failed, so try unmapping the stack
				 */
				vm_unmap_stack(page_size);
			}
			/* No luck, well that's unexpected.. */
			(void)close(fd[1]);
			_exit(EXIT_FAILURE);
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (test_valid && (stress_bogo_get(args) == 0)) {
		pr_fail("%s: no SIGSEGV signals detected\n", args->name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

const stressor_info_t stress_vm_segv_info = {
	.stressor = stress_vm_segv,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
