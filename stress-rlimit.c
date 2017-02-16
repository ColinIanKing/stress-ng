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

#if defined(__linux__)

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
		siglongjmp(jmp_env, 1);
}

/*
 *  stress_rlimit
 *	stress by generating rlimit signals
 */
int stress_rlimit(const args_t *args)
{
	struct rlimit limit;
	struct sigaction old_action_xcpu, old_action_xfsz;
	int fd;
	char filename[PATH_MAX];
	const double start = time_now();

	if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGXFSZ, stress_rlimit_handler, &old_action_xfsz) < 0)
		return EXIT_FAILURE;

	(void)umask(0077);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	if (stress_temp_dir_mk_args(args) < 0)
		return EXIT_FAILURE;
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail_err("creat");
		(void)stress_temp_dir_rm_args(args);
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

		/* Check for timer overrun */
		if ((time_now() - start) > (double)g_opt_timeout)
			break;
		/* Check for counter limit reached */
		if (args->max_ops && *args->counter >= args->max_ops)
			break;

		if (ret == 0) {
			/* Trigger an rlimit signal */
			if (ftruncate(fd, 2) < 0) {
				/* Ignore error */
			}
		} else if (ret == 1) {
			inc_counter(args);	/* SIGSEGV/SIGILL occurred */
		} else {
			break;		/* Something went wrong! */
		}
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGXCPU, &old_action_xcpu);
	(void)stress_sigrestore(args->name, SIGXFSZ, &old_action_xfsz);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}
#else
int stress_rlimit(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
