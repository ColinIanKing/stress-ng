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
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
#include "core-put.h"

#if defined(HAVE_UCONTEXT_H)
#include <ucontext.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"stackmmap N",	   "start N workers exercising a filebacked stack" },
	{ NULL,	"stackmmap-ops N", "stop after N bogo stackmmap operations" },
	{ NULL,	NULL,		   NULL }
};

#if defined(HAVE_SWAPCONTEXT) && 	\
    defined(HAVE_UCONTEXT_H)

#define MMAPSTACK_SIZE		(256 * KB)

/*
 *  stress_stack_check sanity check list
 */
typedef struct stress_stack_check {
	struct stress_stack_check *prev;	/* Previous item on stack list */
	struct stress_stack_check *self_addr;	/* Address of this struct to check */
	uint32_t waste[2];			/* Waste data on stack */
} stress_stack_check_t;

static ucontext_t c_main, c_test;
static uint8_t *stack_mmap;		/* mmap'd stack */
static uintptr_t page_mask;
static size_t page_size;
static int check_status;		/* sanity check status */

/*
 *  push values onto file backed mmap'd stack and
 *  force msync on the map'd region if page boundary
 *  has changed
 */
static void stress_stackmmap_push_msync(stress_stack_check_t *prev_check)
{
	void *addr = (void *)(((uintptr_t)&addr) & page_mask);
	static const void *laddr;
	stress_stack_check_t check;
	register stress_stack_check_t *ptr;
	register int i;
	static const char name[] = "stackmmap";

	check.prev = prev_check;
	check.self_addr = &check;

	/*
	 * Ensure something is written to the stack that
	 * won't get optimized away
	 */
	check.waste[0] = stress_mwc32();
	stress_uint32_put(check.waste[0]);
	check.waste[1] = ~check.waste[0];
	stress_uint32_put(check.waste[1]);
	stress_uint64_put((uint64_t)(intptr_t)&check);

	if (addr != laddr) {
		(void)shim_msync(addr, page_size, stress_mwc1() ? MS_ASYNC : MS_SYNC);
		laddr = addr;
	}

	for (i = 0, ptr = &check; (i < 256) && ptr; ptr = ptr->prev, i++) {
		if (UNLIKELY(ptr->self_addr != ptr)) {
			pr_inf("%s: sanity check address mismatch, got 0x%p, "
				"expecting 0x%p\n", name, ptr, ptr->self_addr);
			check_status = EXIT_FAILURE;
			return;
		}
		if (UNLIKELY(ptr->waste[0] != ~(ptr->waste[1]))) {
			pr_inf("%s: sanity check data mismatch, got 0x%" PRIx32
				", expecting 0x%" PRIx32 "\n", name,
				ptr->waste[0], ptr->waste[1]);
			check_status = EXIT_FAILURE;
			return;
		}
	}

	if (LIKELY(stress_continue_flag()))
		stress_stackmmap_push_msync(&check);

	stress_uint32_put(check.waste[1]);
}

/*
 *  start the push here
 */
static void stress_stackmmap_push_start(void)
{
	stress_stackmmap_push_msync(NULL);
}

/*
 *  stress_stackmmap
 *	stress a file memory map'd stack
 */
static int stress_stackmmap(stress_args_t *args)
{
	int fd, ret;
	volatile int rc = EXIT_FAILURE;		/* could be clobbered */
	char filename[PATH_MAX];
	NOCLOBBER uint8_t *stack_sig;
	struct sigaction new_action;

	page_size = args->page_size;
	page_mask = ~(page_size - 1);

	/* Create file back'd mmaping for the stack */
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fd = open(filename, O_SYNC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_fail("%s: open %s mmap'd stack file failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_dir;
	}
	(void)shim_unlink(filename);
	if (ftruncate(fd, MMAPSTACK_SIZE) < 0) {
		pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		goto tidy_dir;
	}
	stack_sig = (uint8_t *)stress_mmap_populate(NULL, STRESS_SIGSTKSZ,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (stack_sig == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte signal stressor%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, (size_t)STRESS_SIGSTKSZ,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		(void)close(fd);
		goto tidy_dir;
	}
	stress_set_vma_anon_name(stack_sig, STRESS_SIGSTKSZ, "altstack-anon");

	stack_mmap = (uint8_t *)mmap(NULL, MMAPSTACK_SIZE,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (stack_mmap == MAP_FAILED) {
		if (errno == ENXIO) {
			pr_inf_skip("%s: mmap failed of %zu bytes on file %s%s, errno=%d (%s),"
				"skipping stressor\n",
				args->name, (size_t)STRESS_SIGSTKSZ, filename,
				stress_get_memfree_str(), errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			(void)close(fd);
			goto tidy_stack_sig;
		}
		pr_fail("%s: mmap failed of %zu bytes failed%s, errno=%d (%s)\n",
			args->name, (size_t)STRESS_SIGSTKSZ,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)close(fd);
		goto tidy_stack_sig;
	}
	(void)close(fd);
	stress_set_vma_anon_name(stack_mmap, STRESS_SIGSTKSZ, "altstack-file");

	if (shim_madvise((void *)stack_mmap, MMAPSTACK_SIZE, MADV_RANDOM) < 0) {
		pr_dbg("%s: madvise failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
	(void)shim_memset(stack_mmap, 0, MMAPSTACK_SIZE);

#if defined(HAVE_MPROTECT)
	/*
	 *  Make ends of stack inaccessible
	 */
	(void)mprotect((void *)stack_mmap, page_size, PROT_NONE);
	(void)mprotect((void *)(stack_mmap + MMAPSTACK_SIZE - page_size), page_size, PROT_NONE);
#else
	UNEXPECTED
#endif

	(void)shim_memset(&c_test, 0, sizeof(c_test));
	if (getcontext(&c_test) < 0) {
		pr_fail("%s: getcontext failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy_mmap;
	}
	c_test.uc_stack.ss_sp = (void *)(stack_mmap + page_size);
	c_test.uc_stack.ss_size = MMAPSTACK_SIZE - (page_size * 2);
	c_test.uc_link = &c_main;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  set jmp handler to jmp back into the loop on a full
	 *  stack segfault.  Use swapcontext to jump into a
	 *  new context using the new mmap'd stack
	 */
	do {
		pid_t pid;

		(void)stress_mwc32();
again:
		if (UNLIKELY(!stress_continue_flag()))
			break;
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else if (pid > 0) {
			pid_t waitret;
			int status;

			/* Parent, wait for child */
			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				stress_kill_and_wait(args, pid, SIGTERM, false);
			} else {
				if (WIFEXITED(status)) {
					if (WEXITSTATUS(status) != EXIT_SUCCESS) {
						rc = status;
						goto tidy_mmap;
					}
				}
			}
		} else {
			/* Child */

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);

			/*
			 *  We need to handle SEGV signals when we
			 *  hit the end of the mmap'd stack; however
			 *  an alternative signal handling stack
			 *  is required because we ran out of stack
			 */
			(void)shim_memset(&new_action, 0, sizeof new_action);
			new_action.sa_handler = stress_sig_handler_exit;
			(void)sigemptyset(&new_action.sa_mask);
			new_action.sa_flags = SA_ONSTACK;
			if (UNLIKELY(sigaction(SIGSEGV, &new_action, NULL) < 0))
				_exit(EXIT_FAILURE);

			/*
			 *  We need an alternative signal stack
			 *  to handle segfaults on an overrun
			 *  mmap'd stack
			 */
			if (UNLIKELY(stress_sigaltstack(stack_sig, STRESS_SIGSTKSZ) < 0))
				_exit(EXIT_FAILURE);

			check_status = EXIT_SUCCESS;
			(void)makecontext(&c_test, stress_stackmmap_push_start, 0);
			(void)swapcontext(&c_main, &c_test);
			_exit(check_status);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

finish:
	rc = EXIT_SUCCESS;

tidy_mmap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)stack_mmap, MMAPSTACK_SIZE);
tidy_stack_sig:
	(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

const stressor_info_t stress_stackmmap_info = {
	.stressor = stress_stackmmap,
	.classifier = CLASS_VM | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_stackmmap_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without ucontext.h or swapcontext()"
};
#endif
