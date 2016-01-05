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

#if defined(STRESS_SYSFS)

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

#define SYS_BUF_SZ		(4096)
#define MAX_READ_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

typedef struct ctxt {
	const char *name;
	const char *path;
} ctxt_t;

/*
 *  stress_sys_read()
 *	read a proc file
 */
static inline void stress_sys_read(const char *name, const char *path)
{
	int fd;
	ssize_t i = 0;
	char buffer[SYS_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;

	/*
	 *  Multiple randomly sized reads
	 */
	while (i < (4096 * SYS_BUF_SZ)) {
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

	/* file should be R_OK if we've just opened it */
	if ((access(path, R_OK) < 0) &&
	    (opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail(stderr, "%s: R_OK access failed on %s which "
			"could be opened, errno=%d (%s)\n",
			name, path, errno, strerror(errno));
	}
}

/*
 *  stress_sys_read_thread
 *	keep exercising a sysfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_sys_read_thread(void *ctxt_ptr)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ];
        stack_t ss;
	ctxt_t *ctxt = (ctxt_t *)ctxt_ptr;

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
		stress_sys_read(ctxt->name, ctxt->path);

	return &nowt;
}

/*
 *  stress_proc_sys_threads()
 *	create a bunch of threads to thrash read a sys entry
 */
static void stress_sys_read_threads(const char *name, const char *path)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];
	ctxt_t ctxt;

	ctxt.name = name;
	ctxt.path = path;

	memset(ret, 0, sizeof(ret));

	keep_running = true;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_sys_read_thread, &ctxt);
	}
	for (i = 0; i < 8; i++) {
		if (!opt_do_run)
			break;
		stress_sys_read(name, path);
	}
	keep_running = false;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}
}

/*
 *  stress_sys_dir()
 *	read directory
 */
static void stress_sys_dir(
	const char *name,
	const char *path,
	const bool recurse,
	const int depth,
	bool sys_read)
{
	DIR *dp;
	struct dirent *d;

	if (!opt_do_run)
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char filename[PATH_MAX];

		if (!opt_do_run)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				snprintf(filename, sizeof(filename),
					"%s/%s", path, d->d_name);
				stress_sys_dir(name, filename, recurse,
					depth + 1, sys_read);
			}
			break;
		case DT_REG:
			if (sys_read) {
				snprintf(filename, sizeof(filename),
					"%s/%s", path, d->d_name);
				stress_sys_read_threads(name, filename);
			}
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  stress_sysfs
 *	stress reading all of /sys
 */
int stress_sysfs(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	bool sys_read = true;

	(void)instance;

	if (geteuid() == 0) {
		pr_inf(stderr, "%s: running as root, just traversing /sys "
			"and not reading files.\n", name);
		sys_read = false;
	}

	do {
		stress_sys_dir(name, "/sys", true, 0, sys_read);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
