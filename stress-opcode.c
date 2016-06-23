/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_OPCODE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>

#define PAGES		(16)
#define TRACK_SIGCOUNT	(0)

typedef void (*opfunc_t)(void);

static int sigs[] = {
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

#if defined(NSIG)
#define MAX_SIGS	(NSIG)
#elif defined(_NSIG)
#define MAX_SIGS	(_NSIG)
#else
#define MAX_SIGS	(256)
#endif

#if TRACK_SIGCOUNT
static uint64_t *sig_count;
#endif

static void MLOCKED stress_badhandler(int dummy)
{
	(void)dummy;

#if TRACK_SIGCOUNT
	if (dummy < MAX_SIGS)
		sig_count[dummy]++;
#endif
	
	_exit(1);
}

/*
 *  stress_opcode
 *	stress with random opcodes
 */
int stress_opcode(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	int rc = EXIT_FAILURE;
	size_t i;
#if TRACK_SIGCOUNT
	const size_t sig_count_size = MAX_SIGS * sizeof(uint64_t);
#endif

	(void)instance;

#if TRACK_SIGCOUNT
	sig_count = (uint64_t *)mmap(NULL, sig_count_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (sig_count == MAP_FAILED) {
		pr_fail_dbg(name, "mmap");
		return EXIT_NO_RESOURCE;
	}
#endif

	do {
		pid_t pid;

		mwc32();
again:
		if (!opt_do_run)
			break;
		pid = fork();
		if (pid < 0) {
			if (errno == EAGAIN)
				goto again;

			pr_fail_dbg(name, "fork");
			rc = EXIT_NO_RESOURCE;
			goto err;
		}
		if (pid == 0) {
			struct itimerval it;
			uint8_t *opcodes, *ops_begin, *ops_end, *ops;

			for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
				if (stress_sighandler(name, sigs[i], stress_badhandler, NULL) < 0)
				_exit(EXIT_FAILURE);
			}

			opcodes = mmap(NULL, page_size * PAGES, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (opcodes == MAP_FAILED) {
				pr_fail_dbg(name, "mmap");
				_exit(EXIT_NO_RESOURCE);
			}
			/* Force pages resident */
			memset(opcodes, 0x00, page_size * PAGES);

			ops_begin = opcodes + page_size;
			ops_end = opcodes + (page_size * (PAGES - 1));

			(void)mprotect(opcodes, page_size, PROT_NONE);
			(void)mprotect(ops_end, page_size, PROT_NONE);
			(void)mprotect(ops_begin, page_size, PROT_WRITE);
			for (ops = ops_begin; ops < ops_end; ops++) {
				*ops = mwc32();
			}
			(void)mprotect(ops_begin, page_size, PROT_READ | PROT_EXEC);
#if defined(__GNUC__)
			__builtin___clear_cache((char *)ops_begin, (char *)ops_end);
#endif
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			/*
			 * Force abort if the opcodes magically
			 * do an infinite loop
			 */
			it.it_interval.tv_sec = 0;
			it.it_interval.tv_usec = 10000;
			it.it_value.tv_sec = 0;
			it.it_value.tv_usec = 10000;
			if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
				pr_fail_dbg(name, "setitimer");
				_exit(EXIT_NO_RESOURCE);
			}

			((void (*)(void))(ops_begin + mwc8()))();

			(void)munmap(opcodes, page_size * PAGES);
			_exit(0);
		}
		if (pid > 0) {
			int ret, status;

			ret = waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
						name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)waitpid(pid, &status, 0);
			}
			(*counter)++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

#if TRACK_SIGCOUNT
	for (i = 0; i < MAX_SIGS; i++) {
		if (sig_count[i]) {
			pr_dbg(stderr, "%s: %-25.25s: %" PRIu64 "\n",
				name, strsignal(i), sig_count[i]);
		}
	}
#endif
err:
#if TRACK_SIGCOUNT
	(void)munmap(sig_count, sig_count_size);
#endif
	return rc;
}

#endif
