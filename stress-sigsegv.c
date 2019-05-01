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

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile void *fault_addr;
static volatile int signo;
static volatile int code;
#endif

static const help_t help[] = {
	{ NULL,	"sigsegv N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigsegv-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
#if defined(SA_SIGINFO)
static void MLOCKED_TEXT stress_segvhandler(
	int num,
	siginfo_t *info,
	void *ucontext)
{
	(void)num;
	(void)ucontext;

	fault_addr = info->si_addr;
	signo = info->si_signo;
	code = info->si_code;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

/*
 *  stress_sigsegv
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigsegv(const args_t *args)
{
	uint8_t *ptr;
	NOCLOBBER int rc = EXIT_FAILURE;
#if defined(SA_SIGINFO)
	const bool verify = (g_opt_flags & OPT_FLAGS_VERIFY);
#endif

	/* Allocate read only page */
	ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_READ,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		pr_inf("%s: mmap of shared read only page failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	for (;;) {
		int ret;
		struct sigaction action;

		(void)memset(&action, 0, sizeof action);
#if defined(SA_SIGINFO)
		action.sa_sigaction = stress_segvhandler;
#else
		action.sa_handler = stress_segvhandler;
#endif
		(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
		action.sa_flags = SA_SIGINFO;
#endif
		ret = sigaction(SIGSEGV, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGSEGV: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGILL, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGILL: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGBUS, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGBUS: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing())
			break;

		if (ret) {
			/* Signal was tripped */
#if defined(SA_SIGINFO)
			if (verify && fault_addr && fault_addr != ptr) {
				pr_fail("%s: expecting fault address %p, got %p instead\n",
					args->name, fault_addr, ptr);
			}
			if (verify &&
			    (signo != -1) &&
			    (signo != SIGSEGV) &&
			    (signo != SIGILL) &&
			    (signo != SIGBUS)) {
				pr_fail("%s: expecting SIGSEGV/SIGILL/SIGBUS, got %s instead\n",
					args->name, strsignal(signo));
			}
			if (verify && (signo == SIGBUS) && (code != SEGV_ACCERR)) {
				pr_fail("%s: expecting SIGBUS si_code SEGV_ACCERR (%d), got %d instead\n",
					args->name, SEGV_ACCERR, code);
			}
#endif
			inc_counter(args);
		} else {
#if defined(SA_SIGINFO)
			signo = -1;
			code = -1;
			fault_addr = 0;
#endif
			/* Trip a SIGSEGV/SIGILL/SIGBUS */
			*ptr = 0;
		}
	}
	rc = EXIT_SUCCESS;
tidy:
	(void)munmap((void *)ptr, args->page_size);

	return rc;

}

stressor_info_t stress_sigsegv_info = {
	.stressor = stress_sigsegv,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
