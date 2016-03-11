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

#if defined(STRESS_RLIMIT)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

/*
 *  stress_rlimit_handler()
 *	rlimit generic handler
 */
static void MLOCKED stress_rlimit_handler(int dummy)
{
	(void)dummy;

	if (do_jmp)
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_rlimit
 *	stress by generating rlimit signals
 */
int stress_rlimit(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	struct rlimit limit;
	struct sigaction old_action_xcpu, old_action_xfsz;
	int fd;
	char filename[PATH_MAX];
	const pid_t pid = getpid();

	if (stress_sighandler(name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(name, SIGXFSZ, stress_rlimit_handler, &old_action_xfsz) < 0)
		return EXIT_FAILURE;
	(void)umask(0077);
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail_err(name, "creat");
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}
	(void)unlink(filename);

	/* Trigger SIGXCPU every second */
	limit.rlim_cur = 1;
	limit.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CPU, &limit);

	/* Trigger SIGXFSZ every time we truncate file */
	limit.rlim_cur = 1;
	limit.rlim_max = 1;
	setrlimit(RLIMIT_FSIZE, &limit);

	do {
		int ret;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we generate an rlimit signal, so
		 * first check if we need to terminate
		 */
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		if (ret) {
			(*counter)++;	/* SIGSEGV/SIGILL occurred */
		} else {
			/* Trigger an rlimit signal */
			if (ftruncate(fd, 2) < 0) {
				/* Ignore */
			}
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	do_jmp = false;
	(void)stress_sigrestore(name, SIGXCPU, &old_action_xcpu);
	(void)stress_sigrestore(name, SIGXFSZ, &old_action_xfsz);
	(void)close(fd);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}

#endif
