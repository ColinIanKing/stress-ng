/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define OOM_SCORE_ADJ_MIN	"-1000"
#define OOM_SCORE_ADJ_MAX	"1000"

#define OOM_ADJ_NO_OOM		"-17"
#define OOM_ADJ_MIN		"-16"
#define OOM_ADJ_MAX		"15"

/*
 *  stress_process_oomed()
 *	check if a process has been logged as OOM killed
 */
bool stress_process_oomed(const pid_t pid)
{
	int fd;
	bool oomed = false;

	fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return oomed;

	for (;;) {
		char buf[4096], *ptr;
		ssize_t ret;

		ret = read(fd, buf, sizeof(buf) - 1);
		if (ret < 0)
			break;
		buf[ret] = '\0';

		/*
		 * Look for 'Out of memory: Kill process 22566'
		 */
		ptr = strstr(buf, "process");
		if (ptr && (strstr(buf, "Out of memory") ||
			    strstr(buf, "oom_reaper"))) {
			pid_t oom_pid;

			if (sscanf(ptr + 7, "%10d", &oom_pid) == 1) {
				if (oom_pid == pid) {
					oomed = true;
					break;
				}
			}
		}
	}
	(void)close(fd);

	return oomed;
}

/*
 *    stress_set_adjustment()
 *	try to set OOM adjustment, retry if EAGAIN or EINTR, give up
 *	after multiple retries.
 */
static int stress_set_adjustment(const char *procname, const char *name, const char *str)
{
	const size_t len = strlen(str);
	int i;

	for (i = 0; i < 32; i++) {
		ssize_t n;
		int fd;

		fd = open(procname, O_WRONLY);
		if (fd < 0)
			return -1;

		n = write(fd, str, len);
		(void)close(fd);
		if (n > 0)
			return 0;
		if (n < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_dbg("%s: can't set oom_score_adj\n", name);
				return -1;
			}
		}
	}
	/* Unexpected failure, report why */
	pr_dbg("%s: can't set oom_score_adj, errno=%d (%s)\n", name,
		errno, strerror(errno));
	return -1;
}

/*
 *  stress_set_oom_adjustment()
 *	attempt to stop oom killer
 *	if we have root privileges then try and make process
 *	unkillable by oom killer
 */
void stress_set_oom_adjustment(const char *name, const bool killable)
{
	bool high_priv;
	bool make_killable = killable;
	char *str;

	high_priv = (getuid() == 0) && (geteuid() == 0);

	/*
	 *  main cannot be killable; if OPT_FLAGS_OOMABLE set make
	 *  all child procs easily OOMable
	 */
	if (!strcmp(name, "main") && (g_opt_flags & OPT_FLAGS_OOMABLE))
		make_killable = true;

	/*
	 *  Try modern oom interface
	 */
	if (make_killable)
		str = OOM_SCORE_ADJ_MAX;
	else
		str = high_priv ? OOM_SCORE_ADJ_MIN : "0";
	if (stress_set_adjustment("/proc/self/oom_score_adj", name, str) == 0)
		return;
	/*
	 *  Fall back to old oom interface
	 */
	if (make_killable)
		str = high_priv ? OOM_ADJ_NO_OOM : OOM_ADJ_MIN;
	else
		str = OOM_ADJ_MAX;
	(void)stress_set_adjustment("/proc/self/oom_adj", name, str);
}
#else
void stress_set_oom_adjustment(const char *name, const bool killable)
{
	(void)name;
	(void)killable;
}
bool stress_process_oomed(const pid_t pid)
{
	(void)pid;

	return false;
}
#endif

/*
 *  stress_oomable_child()
 *  	generic way to run a process that is possibly going to be
 *	OOM'd and we retry if it gets killed.
 */
int stress_oomable_child(
	const stress_args_t *args,
	void *context,
	stress_oomable_child_func_t func,
	const int flag)
{
	pid_t pid;
	int ooms = 0;
	int segvs = 0;
	int buserrs = 0;

again:
	if (!keep_stressing())
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		/* Keep trying if we are out of resources */
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (pid > 0) {
		/* Parent, wait for child */
		int status, ret;

		(void)setpgid(pid, g_pgrp);
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
			/* Bus error death? retry */
			if (WTERMSIG(status) == SIGBUS) {
                                buserrs++;
                                goto again;
                        }

			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				if (g_opt_flags & OPT_FLAGS_OOMABLE) {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, bailing out "
						"(instance %d)\n",
						args->name, args->instance);
					_exit(0);
				} else {
					stress_log_system_mem_info();
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
		/* Child */

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		stress_set_oom_adjustment(args->name, true);

		/* Explicitly drop capabilites, makes it more OOM-able */
		if (flag & STRESS_OOMABLE_DROP_CAP) {
			int ret;

			ret = stress_drop_capabilities(args->name);
			(void)ret;
		}
		_exit(func(args, context));
	}
	if (ooms + segvs + buserrs > 0)
		pr_dbg("%s: OOM restarts: %d"
			", SIGSEGV restarts: %d"
			", SIGBUS restarts: %d\n",
			args->name, ooms, segvs, buserrs);

	return EXIT_SUCCESS;
}
