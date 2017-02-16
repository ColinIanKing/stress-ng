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

#define MMAP_MAX	(256*1024)

/*
 *  stress_mmapmany()
 *	stress mmap with many pages being mapped
 */
int stress_mmapmany(const args_t *args)
{
	const size_t page_size = args->page_size;
	pid_t pid;
	ssize_t max = sysconf(_SC_MAPPED_FILES);
	uint8_t **mappings;
	max = STRESS_MAXIMUM(max, MMAP_MAX);

	if (max < 1) {
		pr_fail_dbg("sysconf(_SC_MAPPED_FILES)");
		return EXIT_NO_RESOURCE;
	}
	if ((mappings = calloc(max, sizeof(uint8_t *))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}
again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);

		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n", args->name,
					args->instance);
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		ssize_t i, n;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		do {
			for (n = 0; g_keep_stressing_flag && (n < max); n++) {
				if (!keep_stressing())
					break;

				mappings[n] = (uint8_t *)mmap(NULL,
					page_size * 3,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (mappings[n] == MAP_FAILED)
					break;
				if (munmap((void *)(mappings[n] + page_size), page_size) < 0)
					break;
				inc_counter(args);
			}

			for (i = 0; i < n;  i++) {
				munmap((void *)mappings[i], page_size);
				munmap((void *)(mappings[i] + page_size), page_size);
				munmap((void *)(mappings[i] + page_size + page_size), page_size);
			}
		} while (keep_stressing());
	}

	free(mappings);

	return EXIT_SUCCESS;
}
