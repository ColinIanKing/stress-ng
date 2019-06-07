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

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

static const help_t help[] = {
	{ NULL,	"rmap N",	"start N workers that stress reverse mappings" },
	{ NULL,	"rmap-ops N",	"stop after N rmap bogo operations" },
	{ NULL,	NULL,		NULL }
};

#define MAX_RLIMIT_CPU		(1)
#define MAX_RLIMIT_FSIZE	(1)
#define MAX_RLIMIT_AS		(32 * MB)
#define MAX_RLIMIT_DATA		(16 * MB)
#define MAX_RLIMIT_STACK	(1 * MB)
#define MAX_RLIMIT_NOFILE	(32)

typedef struct {
	const int resource;		/* rlimit resource ID */
	const struct rlimit new_limit;	/* new rlimit setting */
	struct rlimit old_limit;	/* original old rlimit setting */
	int ret;			/* saved old rlimit setting return status */
} limits_t;

static limits_t limits[] = {
#if defined(RLIMIT_CPU)
	{ RLIMIT_CPU,	{ MAX_RLIMIT_CPU, MAX_RLIMIT_CPU }, { 0, 0 }, false },
#endif
#if defined(RLIMIT_FSIZE)
	{ RLIMIT_FSIZE,	{ MAX_RLIMIT_FSIZE, MAX_RLIMIT_FSIZE }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_AS)
	{ RLIMIT_AS,	{ MAX_RLIMIT_AS, MAX_RLIMIT_AS }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_DATA)
	{ RLIMIT_DATA,	{ MAX_RLIMIT_DATA, MAX_RLIMIT_DATA }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_STACK)
	{ RLIMIT_STACK,	{ MAX_RLIMIT_STACK, MAX_RLIMIT_STACK }, { 0, 0 }, -1 },
#endif
#if defined(RLIMIT_NOFILE)
	{ RLIMIT_NOFILE,{ MAX_RLIMIT_NOFILE, MAX_RLIMIT_NOFILE }, { 0, 0 }, -1 },
#endif
};

/*
 *  stress_rlimit_handler()
 *	rlimit generic handler
 */
static void MLOCKED_TEXT stress_rlimit_handler(int signum)
{
	(void)signum;

	if (do_jmp)
		siglongjmp(jmp_env, 1);
}

/*
 *  stress_rlimit
 *	stress by generating rlimit signals
 */
static int stress_rlimit(const args_t *args)
{
	struct sigaction old_action_xcpu, old_action_xfsz, old_action_segv;
	int fd, pid;
	size_t i;
	char filename[PATH_MAX];
	const double start = time_now();

	if (stress_sighandler(args->name, SIGSEGV, stress_rlimit_handler, &old_action_segv) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGXFSZ, stress_rlimit_handler, &old_action_xfsz) < 0)
		return EXIT_FAILURE;

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

	for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
		limits[i].ret = getrlimit(limits[i].resource, &limits[i].old_limit);
	}

again:
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	} else if (pid > 0) {
		int status, waitret;

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		waitret = shim_waitpid(pid, &status, 0);
		if (waitret < 0) {
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
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		/* Child rlimit stressor */
		do {
			int ret;

			for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
				(void)setrlimit(limits[i].resource, &limits[i].new_limit);
			}

			ret = sigsetjmp(jmp_env, 1);

			/* Check for timer overrun */
			if ((time_now() - start) > (double)g_opt_timeout)
				break;
			/* Check for counter limit reached */
			if (args->max_ops && get_counter(args) >= args->max_ops)
				break;

			if (ret == 0) {
				uint8_t *ptr;
				void *oldbrk;
				int fds[MAX_RLIMIT_NOFILE];

				switch (mwc32() % 5) {
				default:
				case 0:
					/* Trigger an rlimit signal */
					if (ftruncate(fd, 2) < 0) {
						/* Ignore error */
					}
					break;
				case 1:
					/* Trigger RLIMIT_AS */
					ptr = (uint8_t *)mmap(NULL, MAX_RLIMIT_AS, PROT_READ | PROT_WRITE,
						MAP_ANONYMOUS | MAP_SHARED, -1, 0);
					if (ptr != MAP_FAILED)
						(void)munmap((void *)ptr, MAX_RLIMIT_AS);
					break;
				case 2:
					/* Trigger RLIMIT_DATA */
					oldbrk = shim_sbrk(0);
					if (oldbrk != (void *)-1) {
						ptr = shim_sbrk(MAX_RLIMIT_DATA);
						if (ptr != (void *)-1) {
							int rc = shim_brk(oldbrk);

							(void)rc;
						}
					}
					break;
				case 3:
					/* Trigger RLIMIT_STACK */
					{
						static uint8_t stack[MAX_RLIMIT_STACK];

						mincore_touch_pages_interruptible(stack, MAX_RLIMIT_STACK);
					}
					break;
				case 4:
					/* Hit NOFILE limit */
					for (i = 0; i < MAX_RLIMIT_NOFILE; i++) {
						fds[i] = open("/dev/null", O_RDONLY);
					}
					for (i = 0; i < MAX_RLIMIT_NOFILE; i++) {
						if (fds[i] != -1)
							(void)close(fds[i]);
					}
					break;
				}
			} else if (ret == 1) {
				inc_counter(args);	/* SIGSEGV/SIGILL occurred */
			} else {
				break;		/* Something went wrong! */
			}

			for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
				(void)setrlimit(limits[i].resource, &limits[i].old_limit);
			}
		} while (keep_stressing());

		(void)close(fd);
		_exit(0);
	}

tidy:
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGXCPU, &old_action_xcpu);
	(void)stress_sigrestore(args->name, SIGXFSZ, &old_action_xfsz);
	(void)stress_sigrestore(args->name, SIGSEGV, &old_action_segv);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rlimit_info = {
	.stressor = stress_rlimit,
	.class = CLASS_OS,
	.help = help
};
