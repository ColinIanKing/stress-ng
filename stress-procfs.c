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

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

#define PROC_BUF_SZ		(4096)
#define MAX_READ_THREADS	(4)

typedef struct ctxt {
	const args_t *args;
	const char *path;
	char *badbuf;
	bool proc_write;
} ctxt_t;

static volatile bool keep_running;
static sigset_t set;

/*
 *  stress_proc_rw()
 *	read a proc file
 */
static inline void stress_proc_rw(const char *path, char *badbuf, const bool proc_write)
{
	int fd;
	ssize_t ret, i = 0;
	char buffer[PROC_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	/*
	 *  Multiple randomly sized reads
	 */
	while (i < (4096 * PROC_BUF_SZ)) {
		ssize_t sz = 1 + (mwc32() % sizeof(buffer));
		if (!g_keep_stressing_flag)
			break;
		ret = read(fd, buffer, sz);
		if (ret < 0)
			break;
		if (ret < sz)
			break;
		i += sz;
	}
	(void)close(fd);

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	/*
	 *  Zero sized reads
	 */
	ret = read(fd, buffer, 0);
	if (ret < 0)
		goto err;
	/*
	 *  Bad read buffer
	 */
	if (badbuf) {
		ret = read(fd, badbuf, PROC_BUF_SZ);
		if (ret < 0)
			goto err;
	}
err:
	(void)close(fd);

	/*
	 *  Zero sized writes
	 */
	if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
		return;
	ret = write(fd, buffer, 0);
	(void)ret;
	(void)close(fd);

	if (proc_write) {
		/*
		 *  Zero sized writes
		 */
		if ((fd = open(path, O_WRONLY | O_NONBLOCK)) < 0)
			return;
		ret = write(fd, buffer, 0);
		(void)ret;
		(void)close(fd);
	}
}

/*
 *  stress_proc_rw_thread
 *	keep exercising a procfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_proc_rw_thread(void *ctxt_ptr)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
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
	memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	while (keep_running && g_keep_stressing_flag)
		stress_proc_rw(ctxt->path, ctxt->badbuf, ctxt->proc_write);

	return &nowt;
}

/*
 *  stress_proc_rw_threads()
 *	create a bunch of threads to thrash read a proc entry
 */
static void stress_proc_rw_threads(const args_t *args, char *path, const bool proc_write)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];
	ctxt_t ctxt;

	ctxt.args = args;
	ctxt.path = path;
	ctxt.proc_write = proc_write;
	ctxt.badbuf = mmap(NULL, PROC_BUF_SZ, PROT_READ,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt.badbuf == MAP_FAILED)
		ctxt.badbuf = NULL;

	memset(ret, 0, sizeof(ret));

	keep_running = true;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_proc_rw_thread, &ctxt);
	}
	for (i = 0; i < 8; i++) {
		if (!g_keep_stressing_flag)
			break;
		stress_proc_rw(path, ctxt.badbuf, proc_write);
	}
	keep_running = false;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}

	if (ctxt.badbuf)
		munmap(ctxt.badbuf, PROC_BUF_SZ);
}

/*
 *  stress_proc_dir()
 *	read directory
 */
static void stress_proc_dir(
	const args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const bool proc_write)
{
	DIR *dp;
	struct dirent *d;

	if (!g_keep_stressing_flag)
		return;

	/* Don't want to go too deep */
	if (depth > 8)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!g_keep_stressing_flag)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				(void)snprintf(name, sizeof(name),
					"%s/%s", path, d->d_name);
				stress_proc_dir(args, name, recurse, depth + 1, proc_write);
			}
			break;
		case DT_REG:
			(void)snprintf(name, sizeof(name),
				"%s/%s", path, d->d_name);
			stress_proc_rw_threads(args, name, proc_write);
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
int stress_procfs(const args_t *args)
{
	bool proc_write = true;

	sigfillset(&set);

	if (geteuid() == 0)
		proc_write = false;

	do {
		stress_proc_dir(args, "/proc/self", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/sys", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/sysvipc", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/fs", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/bus", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/irq", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/scsi", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/tty", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/driver", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/tty", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/self", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc/thread_self", true, 0, proc_write);
		inc_counter(args);
		if (!keep_stressing())
			break;

		stress_proc_dir(args,"/proc", false, 0, proc_write);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_procfs(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
