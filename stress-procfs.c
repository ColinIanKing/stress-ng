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

#if defined(STRESS_PROCFS)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#define PROC_BUF_SZ		(4096)
#define MAX_READ_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

/*
 *  stress_proc_read()
 *	read a proc file
 */
static inline void stress_proc_read(const char *path)
{
	int fd;
	ssize_t i = 0;
	char buffer[PROC_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	/*
	 *  Multiple randomly sized reads
	 */
	while (i < (4096 * PROC_BUF_SZ)) {
		ssize_t ret, sz = 1 + (mwc32() % sizeof(buffer));
redo:
		if (!opt_do_run)
			break;
		ret = read(fd, buffer, sz);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			break;
		}
		if (ret < sz)
			break;
		i += sz;
	}
	(void)close(fd);
}

/*
 *  stress_proc_read_thread
 *	keep exercising a procfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_proc_read_thread(void *ctxt)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ];
        stack_t ss;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err("pthread", "sigaltstack");
		return &nowt;
	}
	while (keep_running && opt_do_run)
		stress_proc_read((char *)ctxt);

	return &nowt;
}

/*
 *  stress_proc_read_threads()
 *	create a bunch of threads to thrash read a proc entry
 */
static void stress_proc_read_threads(char *path)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];

	memset(ret, 0, sizeof(ret));

	keep_running = true;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_proc_read_thread, path);
	}
	for (i = 0; i < 8; i++) {
		if (!opt_do_run)
			break;
		stress_proc_read(path);
	}
	keep_running = false;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}
}

/*
 *  stress_proc_dir()
 *	read directory
 */
static void stress_proc_dir(
	const char *path,
	const bool recurse,
	const int depth)
{
	DIR *dp;
	struct dirent *d;

	if (!opt_do_run)
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!opt_do_run)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				snprintf(name, sizeof(name),
					"%s/%s", path, d->d_name);
				stress_proc_dir(name, recurse, depth + 1);
			}
			break;
		case DT_REG:
			snprintf(name, sizeof(name),
				"%s/%s", path, d->d_name);
			stress_proc_read_threads(name);
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  stress_procfs
 *	stress reading all of /proc
 */
int stress_procfs(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	sigfillset(&set);

	do {
		stress_proc_dir("/proc/self", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;


		stress_proc_dir("/proc/sys", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/sysvipc", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/fs", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/bus", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/irq", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/scsi", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/tty", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/driver", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/tty", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/self", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc/thread_self", true, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		stress_proc_dir("/proc", false, 0);
		(*counter)++;
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
