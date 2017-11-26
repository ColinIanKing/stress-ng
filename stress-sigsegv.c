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

static sigjmp_buf jmp_env;

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED stress_segvhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_sigsegv
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
int stress_sigsegv(const args_t *args)
{
	uint8_t *ptr;
	NOCLOBBER int rc = EXIT_FAILURE;

	/* Allocate read only page */
	ptr = mmap(NULL, args->page_size, PROT_READ,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		pr_inf("%s: mmap of shared read only page failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	for (;;) {
		int ret;

		if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
			goto tidy;
		if (stress_sighandler(args->name, SIGILL, stress_segvhandler, NULL) < 0)
			goto tidy;
		if (stress_sighandler(args->name, SIGBUS, stress_segvhandler, NULL) < 0)
			goto tidy;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing())
			break;

		if (ret)
			inc_counter(args);	/* SIGSEGV/SIGILL occurred */
		else
			*ptr = 0;		/* Trip a SIGSEGV/SIGILL */
	}
	rc = EXIT_SUCCESS;
tidy:
	(void)munmap(ptr, args->page_size);

	return rc;

}
