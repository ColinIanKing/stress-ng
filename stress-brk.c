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
	{ NULL,	"brk N",	"start N workers performing rapid brk calls" },
	{ NULL,	"brk-ops N",	"stop after N brk bogo operations" },
	{ NULL,	"brk-notouch",	"don't touch (page in) new data segment page" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_brk_notouch(const char *opt)
{
	(void)opt;
	bool brk_notouch = true;

	return set_setting("brk-notouch", TYPE_ID_BOOL, &brk_notouch);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_brk_notouch,	stress_set_brk_notouch },
	{ 0,			NULL }
};

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
static int stress_brk(const args_t *args)
{
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, nomems = 0;
	const size_t page_size = args->page_size;
	bool brk_notouch = false;

	(void)get_setting("brk-notouch", &brk_notouch);

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
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
				if (g_opt_flags & OPT_FLAGS_OOMABLE) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, bailing out "
						"(instance %d)\n",
						args->name, args->instance);
					_exit(0);
				} else {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, restarting again "
						"(instance %d)\n",
						args->name, args->instance);
					ooms++;
					goto again;
				}
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				segvs++;
				goto again;
			}
		}
	} else if (pid == 0) {
		uint8_t *start_ptr;
		int ret, i = 0;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		/* Explicitly drop capabilites, makes it more OOM-able */
		ret = stress_drop_capabilities(args->name);
		(void)ret;

		start_ptr = shim_sbrk(0);
		if (start_ptr == (void *) -1) {
			pr_fail_err("sbrk(0)");
			_exit(EXIT_FAILURE);
		}

		do {
			uint8_t *ptr;

			i++;
			if (i < 8) {
				/* Expand brk by 1 page */
				ptr = shim_sbrk((intptr_t)page_size);
			} else if (i < 9) {
				/* brk to same brk position */
				ptr = shim_sbrk(0);
				if (shim_brk(ptr) < 0)
					ptr = (void *)-1;
			} else {
				/* Shrink brk by 1 page */
				i = 0;
				ptr = shim_sbrk(0);
				ptr -= page_size;
				if (shim_brk(ptr) < 0)
					ptr = (void *)-1;
			}

			if (ptr == (void *)-1) {
				if ((errno == ENOMEM) || (errno == EAGAIN)) {
					nomems++;
					if (shim_brk(start_ptr) < 0) {
						pr_err("%s: brk(%p) failed: errno=%d (%s)\n",
							args->name, start_ptr, errno,
							strerror(errno));
						_exit(EXIT_FAILURE);
					}
				} else {
					pr_err("%s: sbrk(%d) failed: errno=%d (%s)\n",
						args->name, (int)page_size, errno,
						strerror(errno));
					_exit(EXIT_FAILURE);
				}
			} else {
				/* Touch page, force it to be resident */
				if (!brk_notouch)
					*(ptr - 1) = 0;
			}
			inc_counter(args);
		} while (keep_stressing());
	}
	if (ooms + segvs + nomems > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			args->name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}

stressor_info_t stress_brk_info = {
	.stressor = stress_brk,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
