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

static const help_t help[] = {
	{ NULL,	"mmapfork N",	  "start N workers stressing many forked mmaps/munmaps" },
	{ NULL,	"mmapfork-ops N", "stop after N mmapfork bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_SYS_SYSINFO_H) && defined(HAVE_SYSINFO)

#define MAX_PIDS		(32)

#define _EXIT_FAILURE			(0x01)
#define _EXIT_SEGV_MMAP			(0x02)
#define _EXIT_SEGV_MADV_WILLNEED	(0x04)
#define _EXIT_SEGV_MADV_DONTNEED	(0x08)
#define _EXIT_SEGV_MEMSET		(0x10)
#define _EXIT_SEGV_MUNMAP		(0x20)
#define _EXIT_MASK	(_EXIT_SEGV_MMAP | \
			 _EXIT_SEGV_MADV_WILLNEED | \
			 _EXIT_SEGV_MADV_DONTNEED | \
			 _EXIT_SEGV_MEMSET | \
			 _EXIT_SEGV_MUNMAP)

static volatile int segv_ret;

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	_exit(segv_ret);
}

static void __strlcat(char *dst, char *src, size_t *n)
{
	const size_t ln = strlen(src);

	if (*n <= ln)
		return;

	(void)shim_strlcat(dst, src, *n);
	*n -= ln;
}

/*
 *  stress_mmapfork()
 *	stress mappings + fork VM subystem
 */
static int stress_mmapfork(const args_t *args)
{
	pid_t pids[MAX_PIDS];
	struct sysinfo info;
	void *ptr;
	uint64_t segv_count = 0;
	int8_t segv_reasons = 0;

	do {
		size_t i, n, len;

		(void)memset(pids, 0, sizeof(pids));

		for (n = 0; n < MAX_PIDS; n++) {
retry:			if (!g_keep_stressing_flag)
				goto reap;

			pids[n] = fork();
			if (pids[n] < 0) {
				/* Out of resources for fork, re-do, ugh */
				if ((errno == EAGAIN) || (errno == ENOMEM)) {
					(void)shim_usleep(10000);
					goto retry;
				}
				break;
			}
			if (pids[n] == 0) {
				/* Child */
				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();

				if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
					_exit(_EXIT_FAILURE);

				if (sysinfo(&info) < 0) {
					pr_fail_err("sysinfo");
					_exit(_EXIT_FAILURE);
				}

				len = ((size_t)info.freeram / (args->num_instances * MAX_PIDS)) / 2;
				segv_ret = _EXIT_SEGV_MMAP;
				ptr = mmap(NULL, len, PROT_READ | PROT_WRITE,
					MAP_POPULATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (ptr != MAP_FAILED) {
					segv_ret = _EXIT_SEGV_MADV_WILLNEED;
					(void)shim_madvise(ptr, len, MADV_WILLNEED);

					segv_ret = _EXIT_SEGV_MEMSET;
					(void)memset(ptr, 0, len);

					segv_ret = _EXIT_SEGV_MADV_DONTNEED;
					(void)shim_madvise(ptr, len, MADV_DONTNEED);

					segv_ret = _EXIT_SEGV_MUNMAP;
					(void)munmap(ptr, len);
				}
				_exit(EXIT_SUCCESS);
			}
			(void)setpgid(pids[n], g_pgrp);
		}
reap:
		for (i = 0; i < n; i++) {
			int status;

			if (shim_waitpid(pids[i], &status, 0) < 0) {
				if (errno != EINTR) {
					pr_err("%s: waitpid errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			} else {
				if (WIFEXITED(status)) {
					int masked = WEXITSTATUS(status) & _EXIT_MASK;

					if (masked) {
						segv_count++;
						segv_reasons |= masked;
					}
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	if (segv_count) {
		char buffer[1024];
		size_t n = sizeof(buffer) - 1;

		*buffer = '\0';

		if (segv_reasons & _EXIT_SEGV_MMAP)
			__strlcat(buffer, " mmap", &n);
		if (segv_reasons & _EXIT_SEGV_MADV_WILLNEED)
			__strlcat(buffer, " madvise-WILLNEED", &n);
		if (segv_reasons & _EXIT_SEGV_MADV_DONTNEED)
			__strlcat(buffer, " madvise-DONTNEED", &n);
		if (segv_reasons & _EXIT_SEGV_MEMSET)
			__strlcat(buffer, " memset", &n);
		if (segv_reasons & _EXIT_SEGV_MUNMAP)
			__strlcat(buffer, " munmap", &n);

		pr_dbg("%s: SIGSEGV errors: %" PRIu64 " (where:%s)\n",
			args->name, segv_count, buffer);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_mmapfork_info = {
	.stressor = stress_mmapfork,
	.class = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_mmapfork_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.help = help
};
#endif
