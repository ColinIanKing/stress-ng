/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
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
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-out-of-memory.h"

#define MIN_VFORKMANY_VM_BYTES		(4 * KB)
#define MAX_VFORKMANY_VM_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_VFORKMANY_VM_BYTES	(64 * MB)

typedef struct {
	volatile uint64_t invoked;	/* count of vfork processes that started */
	uint64_t	waited;		/* count of vfork processes waited for */
	volatile bool 	terminate;	/* true to indicate it's time to stop */
	double		t1;		/* time before vfork */
	double		t2;		/* time once vfork process started */
	double		duration;	/* total duration of vfork invocations */
	uint64_t	counter;	/* number duration measurements made */
} vforkmany_shared_t;

static const stress_help_t help[] = {
	{ NULL,	"vforkmany N",     	"start N workers spawning many vfork children" },
	{ NULL,	"vforkmany-ops N", 	"stop after spawning N vfork children" },
	{ NULL, "vforkmany-vm",	   	"enable extra virtual memory pressure" },
	{ NULL, "vforkmany-vm-bytes",	"set the default vm mmap'd size, default 64MB" },
	{ NULL,	NULL,			NULL }
};

/*
 *  vforkmany_wait()
 *	wait and then kill
 */
static void vforkmany_wait(vforkmany_shared_t *vforkmany_shared, const pid_t pid)
{
	for (;;) {
		int ret, status;

		errno = 0;
		ret = waitpid(pid, &status, 0);
		if ((ret >= 0) || (errno != EINTR)) {
			vforkmany_shared->waited++;
			break;
		}

		(void)shim_kill(pid, SIGALRM);
	}
}

/*
 *  stress_vforkmany()
 *	stress by vfork'ing as many processes as possible.
 *	vfork has interesting semantics, the parent blocks
 *	until the child has exited, plus child processes
 *	share the same address space. So we need to be
 *	careful not to overwrite shared variables across
 *	all the processes.
 */
static int stress_vforkmany(stress_args_t *args)
{
	/* avoid variables on stack since we're using vfork */
	static pid_t chpid;
	static uint8_t *stack_sig;
	static bool vforkmany_vm = false;
	static vforkmany_shared_t *vforkmany_shared;
	static size_t vforkmany_vm_bytes = 0;
	static int rc = EXIT_SUCCESS;

	(void)stress_get_setting("vforkmany-vm", &vforkmany_vm);
	(void)stress_get_setting("vforkmany-vm-bytes", &vforkmany_vm_bytes);
	if (vforkmany_vm_bytes != 0) {
		/* vm bytes supplied so enable vforkmany_vm */
		vforkmany_vm = true;
	} else {
		vforkmany_vm_bytes = DEFAULT_VFORKMANY_VM_BYTES;
	}

	stress_ksm_memory_merge(1);

	/* We should use an alternative signal stack */
	stack_sig = (uint8_t *)stress_mmap_populate(NULL, STRESS_SIGSTKSZ,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (stack_sig == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot allocate signal stack,"
			" errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(stack_sig, STRESS_SIGSTKSZ, "altstack");
	if (stress_sigaltstack(stack_sig, STRESS_SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	vforkmany_shared = (vforkmany_shared_t *)
		stress_mmap_populate(NULL, sizeof(*vforkmany_shared),
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (vforkmany_shared == MAP_FAILED) {
		pr_inf("%s: mmap failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		VOID_RET(int, stress_sigaltstack(NULL, 0));
		(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(vforkmany_shared, sizeof(*vforkmany_shared), "state");
	vforkmany_shared->terminate = false;
	vforkmany_shared->invoked = false;
	vforkmany_shared->waited = false;
	vforkmany_shared->t1 = 0.0;
	vforkmany_shared->t2 = 0.0;
	vforkmany_shared->duration = 0.0;
	vforkmany_shared->counter = 0;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

fork_again:
	chpid = fork();
	if (chpid < 0) {
		if (stress_redo_fork(args, errno))
			goto fork_again;
		if (!stress_continue(args))
			goto finish;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)vforkmany_shared, sizeof(*vforkmany_shared));
		VOID_RET(int, stress_sigaltstack(NULL, 0));
		(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);
		return EXIT_FAILURE;
	} else if (chpid == 0) {
		static uint8_t *waste;
		static size_t waste_size;

		/*
		 *  We want the children to be OOM'd if we
		 *  eat up too much memory
		 */
		stress_set_oom_adjustment(args, true);
		stress_parent_died_alarm();

		/*
		 *  Allocate some wasted space so this child
		 *  scores more on the OOMable score than the
		 *  parent waiter so in theory it should be
		 *  OOM'd before the parent.
		 */
		waste_size = vforkmany_vm_bytes;
		do {
			waste = (uint8_t *)mmap(NULL, waste_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (waste != MAP_FAILED)
				break;
			if (!stress_continue_flag())
				_exit(0);
			waste_size >>= 1;
		} while (waste_size > 4096);

		if (waste != MAP_FAILED) {
			if (waste_size != vforkmany_vm_bytes) {
				static char buf[32];

				stress_uint64_to_str(buf, sizeof(buf), (uint64_t)waste_size);
				pr_dbg("%s: could only mmap a region of size of %s\n", args->name, buf);
			}
			(void)stress_mincore_touch_pages_interruptible(waste, waste_size);
		}
		if (!stress_continue_flag())
			_exit(0);

		if (vforkmany_vm) {
			int flags = 0;

#if defined(MADV_NORMAL)
			flags |= MADV_NORMAL;
#endif
#if defined(MADV_HUGEPAGE)
			flags |= MADV_HUGEPAGE;
#endif
#if defined(MADV_SEQUENTIAL)
			flags |= MADV_SEQUENTIAL;
#endif
			if (flags)
				stress_madvise_pid_all_pages(getpid(), flags);
		}

		do {
			/*
			 *  Force pid to be a register, if it's
			 *  stashed on the stack or as a global
			 *  then waitpid will pick up the one
			 *  shared by all the vfork children
			 *  which is problematic on the wait
			 *  This is a dirty hack.
			 */
			register pid_t pid;
			static volatile pid_t start_pid = -1;

vfork_again:
			/*
			 * SIGALRM is not inherited over vfork so
			 * instead poll the run time and break out
			 * of the loop if we've run out of run time
			 */
			if (vforkmany_shared->terminate) {
				stress_continue_set_flag(false);
				break;
			}
			if (start_pid == -1) {
				pid = fork();
				if (pid >= 0)
					start_pid = getpid();
			} else {
				vforkmany_shared->t1 = stress_time_now();
				pid = shim_vfork();
				stress_bogo_inc(args);
			}
			if (pid < 0) {
				/* failed */
				(void)shim_sched_yield();
				_exit(0);
			} else if (pid == 0) {
				vforkmany_shared->invoked++;
				if (vforkmany_shared->t1 > 0.0) {
					vforkmany_shared->t2 = stress_time_now();
					if (vforkmany_shared->t2 > vforkmany_shared->t1) {
						vforkmany_shared->counter++;
						vforkmany_shared->duration +=
							vforkmany_shared->t2 - vforkmany_shared->t1;
					}
				}

				if (vforkmany_vm) {
					int flags = 0;

#if defined(MADV_MERGEABLE)
					flags |= MADV_MERGEABLE;
#endif
#if defined(MADV_WILLNEED)
					flags |= MADV_WILLNEED;
#endif
#if defined(MADV_HUGEPAGE)
					flags |= MADV_HUGEPAGE;
#endif
#if defined(MADV_RANDOM)
					flags |= MADV_RANDOM;
#endif
					if (flags)
						stress_madvise_pid_all_pages(getpid(), flags);
				}
				if (waste != MAP_FAILED)
					(void)stress_mincore_touch_pages_interruptible(waste, waste_size);

				/* child, parent is blocked, spawn new child */
				if (!args->max_ops || (stress_bogo_get(args) < args->max_ops))
					goto vfork_again;
				_exit(0);
			} else {
				/* parent, wait for child, and exit if not first parent */
				if (pid >= 1)
					(void)vforkmany_wait(vforkmany_shared, pid);
				(void)shim_sched_yield();
				if (getpid() != start_pid)
					_exit(0);
			}
		} while (stress_continue(args));

		if (waste != MAP_FAILED)
			(void)munmap((void *)waste, waste_size);

		_exit(0);
	} else {
		/*
		 * Parent sleeps until timeout/SIGALRM and then
		 * flags terminate state that the vfork children
		 * see and will then exit.  We wait for the first
		 * one spawned to unblock and exit
		 */
		g_opt_flags &= ~OPT_FLAGS_OOMABLE;
		stress_set_oom_adjustment(args, false);

		(void)sleep((unsigned int)g_opt_timeout);
		vforkmany_shared->terminate = true;

		stress_kill_and_wait(args, chpid, SIGALRM, false);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);


	if (vforkmany_shared->counter) {
		double rate = vforkmany_shared->duration / (double)vforkmany_shared->counter;

		stress_metrics_set(args, 0, "nanosecs to start vfork'd a process",
			rate * 1000000000.0, STRESS_METRIC_HARMONIC_MEAN);
	}
	if ((vforkmany_shared->waited > 0) && (vforkmany_shared->invoked == 0)) {
		pr_fail("%s: no vfork'd processes got fully invoked correctly "
			"before they terminated\n", args->name);
		rc = EXIT_FAILURE;
	}

	VOID_RET(int, stress_sigaltstack(NULL, 0));
	(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);
	(void)munmap((void *)vforkmany_shared, sizeof(*vforkmany_shared));

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_vforkmany_vm,       "vforkmany-vm",       TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vforkmany_vm_bytes, "vforkmany-vm-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_VFORKMANY_VM_BYTES, MAX_VFORKMANY_VM_BYTES, NULL },
	END_OPT,
};

const stressor_info_t stress_vforkmany_info = {
	.stressor = stress_vforkmany,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
