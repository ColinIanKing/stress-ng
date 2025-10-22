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
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
#include "core-put.h"
#include "core-pragma.h"

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#define STRESS_BAD_ALTSTACK_SIZE	(65536)

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
#if defined(O_TMPFILE)
static void *bus_stack;
#endif
static sigjmp_buf jmpbuf;
static size_t stress_minsigstksz;

STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static void stress_bad_altstack_force_fault(uint8_t *stack_start)
{
	volatile uint8_t *vol_stack = (volatile uint8_t *)stack_start;
	/* trigger segfault on stack */

	stress_uint8_put(*vol_stack);	/* cppcheck-suppress nullPointer */
	*vol_stack = 0;			/* cppcheck-suppress nullPointer */
	(void)*vol_stack;		/* cppcheck-suppress nullPointer */
}
STRESS_PRAGMA_POP

#if defined(SIGXCPU) &&	\
    defined(RLIMIT_CPU)
static void NORETURN MLOCKED_TEXT stress_xcpu_handler(int signum)
{
	(void)signum;

	_exit(0);
}
#endif

static void NORETURN MLOCKED_TEXT stress_signal_handler(int signum)
{
	uint8_t data[STRESS_BAD_ALTSTACK_SIZE];

	/*
	 * Linux does not allow setting the alternative stack
	 * while in this context. Not sure if this is portable
	 * so ignore this for now.
	{
		VOID_RET(int, stress_sigaltstack_no_check(stack, STRESS_BAD_ALTSTACK_SIZE));
	}
	 */

	(void)signum;
	(void)munmap(stack, stress_minsigstksz);
	(void)shim_memset(data, 0xff, sizeof(data));
	stress_uint8_put(data[0]);

	if (zero_stack != MAP_FAILED)
		(void)munmap(zero_stack, stress_minsigstksz);
#if defined(O_TMPFILE)
	if (bus_stack != MAP_FAILED)
		(void)munmap(bus_stack, stress_minsigstksz);
#else
	UNEXPECTED
#endif
	/*
	 *  If we've not got this far we've not
	 *  generated a fault inside the stack of the
	 *  signal handler, so jmp back and re-try
	 */
	siglongjmp(jmpbuf, 1);
	stress_no_return();
}

static int stress_bad_altstack_child(stress_args_t *args)
{
#if defined(HAVE_VDSO_VIA_GETAUXVAL)
	unsigned long int vdso = getauxval(AT_SYSINFO_EHDR);
#else
	UNEXPECTED
#endif
	NOCLOBBER uint32_t rnd;
	int i, ret;
	stack_t ss, old_ss;
	size_t sz;
#if defined(SIGXCPU) &&	\
    defined(RLIMIT_CPU)
	struct rlimit rlim;
#endif

	if (sigsetjmp(jmpbuf, 1) != 0) {
		/*
		 *  We land here if we get a segfault
		 *  but not a segfault in the sighandler
		 *  ..bail out fast as we can
		 */
		if (UNLIKELY(!stress_continue(args)))
			_exit(EXIT_SUCCESS);
	}

	/* Exercise fetch of old ss, return 0 */
	(void)sigaltstack(NULL, &old_ss);

	/* Exercise disable SS_DISABLE */
	ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
	ss.ss_size = stress_minsigstksz;
	ss.ss_flags = SS_DISABLE;
	(void)sigaltstack(&ss, NULL);

	/* Exercise invalid flags */
	ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
	ss.ss_size = stress_minsigstksz;
	ss.ss_flags = ~0;
	(void)sigaltstack(&ss, NULL);

	/* Exercise no-op, return 0 */
	(void)sigaltstack(NULL, NULL);

	/* Exercise less than minimum allowed stack size, ENOMEM */
	ss.ss_sp = stress_align_address(stack, STACK_ALIGNMENT);
	ss.ss_size = stress_minsigstksz - 1;
	ss.ss_flags = 0;
	(void)sigaltstack(&ss, NULL);

#if defined(SIGSEGV)
	if (stress_sighandler(args->name, SIGSEGV, stress_signal_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif
#if defined(SIGBUS)
	if (stress_sighandler(args->name, SIGBUS, stress_signal_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif
#if defined(SIGILL)
	/* Some BSD kernels trigger SIGILL on bad alternative stack jmps */
	if (stress_sighandler(args->name, SIGILL, stress_signal_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif
#if defined(SIGXCPU) &&	\
    defined(RLIMIT_CPU)
	if (stress_sighandler(args->name, SIGXCPU, stress_xcpu_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	rlim.rlim_cur = 1;
	rlim.rlim_max = 1;
	(void)setrlimit(RLIMIT_CPU, &rlim);
#else
	UNEXPECTED
#endif

	/* Set alternative stack for testing */
	if (stress_sigaltstack_no_check(stack, stress_minsigstksz) < 0) {
		/*
		 *  Pretend it's all OK, for example OpenBSD can fail
		 *  depending on the stack setting on some of test cases
		 *  so don't flag it's a failure per se
		 */
		return EXIT_SUCCESS;
	}

	/* Child */
	stress_mwc_reseed();

	stress_set_oom_adjustment(args, true);
	stress_process_dumpable(false);
	(void)sched_settings_apply(true);

	for (i = 0; i < 10; i++) {
retry:
		if (UNLIKELY(!stress_continue(args)))
			return EXIT_SUCCESS;
		rnd = stress_mwc32modn(11);
		switch (rnd) {
#if defined(HAVE_MPROTECT)
		case 1:
			/* Illegal stack with no protection */
			ret = mprotect(stack, stress_minsigstksz, PROT_NONE);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
		case 2:
			/* Illegal read-only stack */
			ret = mprotect(stack, stress_minsigstksz, PROT_READ);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
		case 3:
			/* Illegal exec-only stack */
			ret = mprotect(stack, stress_minsigstksz, PROT_EXEC);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
		case 4:
			/* Illegal write-only stack */
			ret = mprotect(stack, stress_minsigstksz, PROT_WRITE);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
#else
		case 1..4:
			goto retry;
#endif
		case 5:
			/* Illegal NULL stack */
			ret = stress_sigaltstack_no_check(NULL, STRESS_SIGSTKSZ);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
		case 6:
			/* Illegal text segment stack */
			ret = stress_sigaltstack_no_check(stress_signal_handler, STRESS_SIGSTKSZ);
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			goto retry;
		case 7:
			/* Small stack */
			for (ret = -1, sz = 0; sz <= STRESS_SIGSTKSZ; sz += 256) {
				ret = stress_sigaltstack_no_check(stack, sz);
				if (ret == 0)
					break;
			}
			if (ret == 0)
				stress_bad_altstack_force_fault(stack);
			stress_bad_altstack_force_fault(g_shared->null_ptr);
			goto retry;
		case 8:
#if defined(HAVE_VDSO_VIA_GETAUXVAL)
			/* Illegal stack on VDSO, otherwises NULL stack */
			if (vdso) {
				ret = stress_sigaltstack_no_check((void *)vdso, STRESS_SIGSTKSZ);
				if (ret == 0)
					stress_bad_altstack_force_fault(stack);
			}
#endif
			goto retry;
		case 9:
			/* Illegal /dev/zero mapped stack */
			if (zero_stack != MAP_FAILED) {
				ret = stress_sigaltstack_no_check(zero_stack, stress_minsigstksz);
				if (ret == 0)
					stress_bad_altstack_force_fault(zero_stack);
			}
			goto retry;
		case 10:
#if defined(O_TMPFILE)
			/* Illegal mapped stack to empty file, causes BUS error */
			if (bus_stack != MAP_FAILED) {
				ret = stress_sigaltstack_no_check(bus_stack, stress_minsigstksz);
				if (ret == 0)
					stress_bad_altstack_force_fault(bus_stack);
			}
#endif
			goto retry;
		default:
		case 0:
			/* Illegal unmapped stack */
			(void)munmap(stack, stress_minsigstksz);
			stress_bad_altstack_force_fault(g_shared->null_ptr);
			break;
		}
	}
	/* No luck, well that's unexpected.. */
	if (UNLIKELY(!stress_continue(args)))
		pr_fail("%s: child process with illegal stack unexpectedly worked, %d\n",
			args->name, rnd);
	return EXIT_FAILURE;
}

/*
 *  stress_bad_altstack()
 *	create bad alternative signal stacks and cause
 *	a SIGSEGV when handling SIGSEGVs. The kernel
 *	should kill these.
 */
static int stress_bad_altstack(stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS;
#if defined(O_TMPFILE)
	int tmp_fd;
#endif
#if defined(MAP_STACK)
	const int map_stackflags = MAP_STACK;
#else
	const int map_stackflags = 0;
#endif

	stress_minsigstksz = STRESS_MINSIGSTKSZ;
	stress_set_oom_adjustment(args, true);

	stack = stress_mmap_populate(NULL, stress_minsigstksz,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | map_stackflags, -1, 0);
	if (stack == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu byte signal handler stack%s, "
			    "errno=%d (%s), skipping stressor\n",
			args->name, (size_t)stress_minsigstksz,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(stack, stress_minsigstksz, "altstack");
	(void)stress_madvise_mergeable(stack, stress_minsigstksz);

#if defined(O_TMPFILE)
	tmp_fd = open(stress_get_temp_path(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
	if (tmp_fd < 0) {
		bus_stack = MAP_FAILED;
	} else {
		bus_stack = stress_mmap_populate(NULL,
				stress_minsigstksz,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | map_stackflags, tmp_fd, 0);
		(void)close(tmp_fd);
		if (bus_stack != MAP_FAILED)
			stress_set_vma_anon_name(bus_stack, stress_minsigstksz, "altstack-bus-errors");
	}
#else
	UNEXPECTED
#endif

	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0) {
		zero_stack = MAP_FAILED;
	} else {
		zero_stack = stress_mmap_populate(NULL,
			stress_minsigstksz, PROT_READ,
			MAP_PRIVATE, fd, 0);
		(void)close(fd);
		if (zero_stack != MAP_FAILED)
			stress_set_vma_anon_name(zero_stack, stress_minsigstksz, "altstack-zero-ro");
	}

	/*
	 *  Ensure that mumap is fixed up by the dynamic loader
	 *  before it needs to be called in the signal handler.
	 *  If the alternative stack is broken then sorting out the
	 *  symbol with lazy dl fixup breaks sparc64, so force
	 *  fixup with an illegal munmap call
	 */
	(void)munmap(MAP_FAILED, 0);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

		(void)stress_mwc32();
again:
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				return EXIT_SUCCESS;
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (pid > 0) {
			int status;
			pid_t ret;

			/* Parent, wait for child */
			ret = waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
				(void)stress_kill_pid_wait(pid, NULL);
			} else if (WIFSIGNALED(status)) {
				/* If we got killed by OOM killer, re-start */
				if (WTERMSIG(status) == SIGKILL) {
					if (g_opt_flags & OPT_FLAGS_OOMABLE) {
						stress_log_system_mem_info();
						pr_dbg("%s: assuming killed by OOM "
							"killer, bailing out "
							"(instance %d)\n",
							args->name, args->instance);
						rc = EXIT_SUCCESS;
						goto finish;
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
					stress_bogo_inc(args);
			} else if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) != EXIT_SUCCESS) {
					rc = WEXITSTATUS(status);
					goto finish;
				}
			}
		} else {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			_exit(stress_bad_altstack_child(args));
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(O_TMPFILE)
	if (bus_stack != MAP_FAILED)
		(void)munmap(bus_stack, stress_minsigstksz);
#else
	UNEXPECTED
#endif
	if (zero_stack != MAP_FAILED)
		(void)munmap(zero_stack, stress_minsigstksz);
	if (stack != MAP_FAILED)
		(void)munmap(stack, stress_minsigstksz);

	return rc;
}

const stressor_info_t stress_bad_altstack_info = {
	.stressor = stress_bad_altstack,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_bad_altstack_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sigaltstack()"
};
#endif
