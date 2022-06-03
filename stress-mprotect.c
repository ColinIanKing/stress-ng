/*
 * Copyright (C)      2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"mprotect N",	 "start N workers exercising mprotect on memory" },
	{ NULL,	"mprotect-ops N", "stop after N bogo mprotect operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_MPROTECT)

#define MPROTECT_MAX	(7)

static sigjmp_buf jmp_env;

static void NORETURN MLOCKED_TEXT stress_sig_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
}

static void stress_mprotect_mem(
	const stress_args_t *args,
	const size_t page_size,
	uint8_t *mem,
	const size_t mem_pages,
	const int *prot_flags,
	const int n_flags)
{
	int ret;
	const uint8_t *mem_end = mem + (page_size * mem_pages);

	if (stress_sighandler(args->name, SIGSEGV, stress_sig_handler, NULL) < 0)
		return;
	if (stress_sighandler(args->name, SIGBUS, stress_sig_handler, NULL) < 0)
		return;

	ret = sigsetjmp(jmp_env, 1);
	(void)ret;

	while (keep_stressing(args)) {
		const uint32_t page = stress_mwc32() % mem_pages;
		uint8_t *ptr = mem + (page_size * page);
		const size_t max_size = mem_end - ptr;
		const size_t size = stress_mwc32() % max_size;
		int i;

		for (i = 0; (i < 10) && keep_stressing(args); i++) {
			const uint8_t j = stress_mwc16() % n_flags;

			if (mprotect((void *)ptr, size, prot_flags[j]) == 0) {
				inc_counter(args);
				*ptr |= 1;
				break;
			}
		}
	}
}

/*
 *  stress_mprotect()
 *	stress mprotect
 */
static int stress_mprotect(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t mem_pages = (MPROTECT_MAX >> 1) + 1;
	const size_t mem_size = page_size * mem_pages;
	size_t i;
	uint8_t *mem;
	pid_t pids[MPROTECT_MAX];
	int prot_bits = 0, n_flags, *prot_flags;

#if defined(PROT_NONE)
	prot_bits |= PROT_NONE;
#endif
#if defined(PROT_READ)
	prot_bits |= PROT_READ;
#endif
#if defined(PROT_WRITE)
	prot_bits |= PROT_WRITE;
#endif
#if defined(PROT_SEM)
	prot_bits |= PROT_SEM;
#endif
#if defined(PROT_SAO)
	prot_bits |= PROT_SAO;
#endif
#if defined(PROT_GROWSUP)
	prot_bits |= PROT_GROWSUP;
#endif
#if defined(PROT_GROWSDOWN)
	prot_bits |= PROT_GROWSDOWN;
#endif

	n_flags = stress_flag_permutation(prot_bits, &prot_flags);
	if (!prot_flags) {
		pr_inf("%s: cannot allocate protection masks, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	mem = (uint8_t *)mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (mem == MAP_FAILED) {
		pr_inf("%s: cannot allocate %zd pages, skipping stressor\n",
			args->name, mem_pages);
		free(prot_flags);
		return EXIT_NO_RESOURCE;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args->name, true);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < MPROTECT_MAX; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			stress_mprotect_mem(args, page_size, mem, mem_pages, prot_flags, n_flags);
			_exit(EXIT_SUCCESS);
		}
	}

	stress_mprotect_mem(args, page_size, mem, mem_pages, prot_flags, n_flags);


	for (i = 0; i < MPROTECT_MAX; i++) {
		int status;

		if (pids[i] <= 1)
			continue;
		(void)kill(pids[i], SIGKILL);
		(void)waitpid(pids[i], &status, 0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(mem, mem_size);
	free(prot_flags);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mprotect_info = {
	.stressor = stress_mprotect,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_mprotect_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
#endif
