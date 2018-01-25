/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#if defined(__linux__)

#define HASH_SYSCALL_SIZE	(1987)

#if defined(__NR_syscalls)
#define MAX_SYSCALL	__NR_syscalls
#else
#define MAX_SYSCALL	(2048)		/* Guess */
#endif

typedef struct hash_syscall {
	struct hash_syscall *next;
	long	number;
} hash_syscall_t;

static hash_syscall_t *hash_syscall_table[HASH_SYSCALL_SIZE];

static inline bool HOT OPTIMIZE3 syscall_find(long number)
{
	hash_syscall_t *h = hash_syscall_table[number % HASH_SYSCALL_SIZE];

	while (h) {
		if (h->number == number)
			return true;
		h = h->next;
	}
	return false;
}

static inline void HOT OPTIMIZE3 syscall_add(long number)
{
	const long hash = number % HASH_SYSCALL_SIZE;
	hash_syscall_t *newh;

	newh = malloc(sizeof(*newh));
	if (!newh)
		return;	/* Can't add, ignore */

	newh->number = number;
	newh->next = hash_syscall_table[hash];
	hash_syscall_table[hash]  = newh;
}

static inline void syscall_free(void)
{
	size_t i;
	hash_syscall_t *h;

	for (i = 0; i < HASH_SYSCALL_SIZE; i++) {
		h = hash_syscall_table[i];

		while (h) {
			hash_syscall_t *next = h->next;

			free(h);
			h = next;
		}
	}
}

static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGEMT)
	SIGEMT,
#endif
#if defined(SIGALRM)
	SIGALRM,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGHUP)
	SIGHUP
#endif
};

static const long skip_syscalls[] = {
#if defined(__NR_fork)
	__NR_fork,
#endif
#if defined(__NR_clone)
	__NR_clone,
#endif
#if defined(__NR_exit)
	__NR_exit,
#endif
#if defined(__NR_rt_sigreturn)
	__NR_rt_sigreturn,
#endif
#if defined(__NR_sigreturn)
	__NR_sigreturn,
#endif
#if defined(__linux__) && defined(STRESS_X86)
	513,	/* sys32_x32_rt_sigreturn */
#endif
#if defined(__NR_compat_sigreturn)
	__NR_compat_sigreturn,
#endif
#if defined(__NR_wait)
	__NR_wait,
#endif
#if defined(__NR_wait4)
	__NR_wait4,
#endif
#if defined(__NR_waitpid)
	__NR_waitpid,
#endif
#if defined(__NR_waitid)
	__NR_waitid,
#endif
#if defined(__NR_exit_group)
	__NR_exit_group,
#endif
#if defined(__NR_kill)
	__NR_kill,
#endif
#if defined(__NR_execve)
	__NR_execve,
#endif
#if defined(__NR_reboot)
	__NR_reboot,
#endif
#if defined(__NR_restart_syscall)
	__NR_restart_syscall,
#endif
	0	/* ensure at least 1 element */
};

static void MLOCKED stress_badhandler(int signum)
{
	(void)signum;
	_exit(1);
}

/*
 *  Call a system call in a child context so we don't clobber
 *  the parent
 */
static inline int stress_do_syscall(const args_t *args, const long number)
{
	pid_t pid;
	int rc = 0;

	/* Check if this is a known non-ENOSYS syscall */
	if (syscall_find(number))
		return rc;
again:
	if (!g_keep_stressing_flag)
		return 0;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;

		pr_fail_dbg("fork");
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		struct itimerval it;
		int ret;
		size_t i;

		/* We don't want bad ops clobbering this region */
		stress_unmap_shared();

		/* Drop all capabilities */
		if (stress_drop_capabilities(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}
		for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
			if (stress_sighandler(args->name, sigs[i], stress_badhandler, NULL) < 0)
				_exit(EXIT_FAILURE);
		}

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/*
		 * Force abort if we take too long
		 */
		it.it_interval.tv_sec = 0;
		it.it_interval.tv_usec = 100000;
		it.it_value.tv_sec = 0;
		it.it_value.tv_usec = 100000;
		if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
			pr_fail_dbg("setitimer");
			_exit(EXIT_NO_RESOURCE);
		}
		ret = syscall(number, -1, -1, -1, -1, -1, -1, -1);
		_exit(ret < 0 ? errno : 0);
	} else {
		int ret, status;

		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);

		}
		rc = WEXITSTATUS(status);

		/* Add to known syscalls that don't return ENOSYS */
		if (rc != ENOSYS)
			syscall_add(number);
		inc_counter(args);
	}
	return rc;
}

/*
 *  stress_enosys
 *	stress system calls
 */
int stress_enosys(const args_t *args)
{
	const unsigned long mask = ULONG_MAX;
	size_t n;

	for (n = 0; n < SIZEOF_ARRAY(skip_syscalls) - 1; n++) {
		syscall_add(skip_syscalls[n]);
	}


	do {
		long number;
		int i;

		/* Low sequential syscalls */
		for (number = 0; number < MAX_SYSCALL + 1024; number++) {
			if (!keep_stressing())
				goto finish;
			stress_do_syscall(args, number);
		}

		/* Random syscalls */
		for (i = 0; i < 1024; i++) {
			if (!keep_stressing())
				goto finish;
			stress_do_syscall(args, mwc8() & mask);
			stress_do_syscall(args, mwc16() & mask);
			stress_do_syscall(args, mwc32() & mask);
			stress_do_syscall(args, mwc64() & mask);
		}

		/* Various bit masks */
		for (number = 1; number; number <<= 1) {
			if (!keep_stressing())
				goto finish;
			stress_do_syscall(args, number);
			stress_do_syscall(args, number | 1);
			stress_do_syscall(args, number | (number << 1));
			stress_do_syscall(args, ~number);
		}

		/* Various high syscalls */
		for (number = 0xff; number; number <<= 8) {
			long n;

			for (n = 0; n < 0x100; n++) {
				if (!keep_stressing())
					goto finish;
				stress_do_syscall(args, n + number);
			}
		}
	} while (keep_stressing());

finish:
	syscall_free();

	return EXIT_SUCCESS;
}
#else
int stress_enosys(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
