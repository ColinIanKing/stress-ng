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

#define SYS_BUF_SZ		(4096)
#define MAX_READ_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

typedef struct ctxt {
	const args_t *args;
	const char *path;
	char *badbuf;
} ctxt_t;

/*
 *  stress_sys_rw()
 *	read a proc file
 */
static inline void stress_sys_rw(
	const args_t *args,
	const char *path,
	char *badbuf)
{
	int fd;
	ssize_t i = 0, ret;
	char buffer[SYS_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;

	/*
	 *  Multiple randomly sized reads
	 */
	while (i < (4096 * SYS_BUF_SZ)) {
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

	/* file stat should be OK if we've just opened it */
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		struct stat buf;

		if (fstat(fd, &buf) < 0) {
			pr_fail_err("stat");
		} else {
			if ((buf.st_mode & S_IROTH) == 0) {
				pr_fail("%s: read access failed on %s which "
					"could be opened, errno=%d (%s)\n",
				args->name, path, errno, strerror(errno));
			}
		}
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
		ret = read(fd, badbuf, SYS_BUF_SZ);
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
}

/*
 *  stress_sys_rw_thread
 *	keep exercising a sysfs entry until
 *	controlling thread triggers an exit
 */
static void *stress_sys_rw_thread(void *ctxt_ptr)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	ctxt_t *ctxt = (ctxt_t *)ctxt_ptr;
	const args_t *args = ctxt->args;

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
		stress_sys_rw(args, ctxt->path, ctxt->badbuf);

	return &nowt;
}

/*
 *  stress_proc_sys_threads()
 *	create a bunch of threads to thrash read a sys entry
 */
static void stress_sys_rw_threads(const args_t *args, const char *path)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];
	ctxt_t ctxt;

	ctxt.args = args;
	ctxt.path = path;

	memset(ret, 0, sizeof(ret));

	ctxt.badbuf = mmap(NULL, SYS_BUF_SZ, PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt.badbuf == MAP_FAILED)
		ctxt.badbuf = NULL;

	keep_running = true;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_sys_rw_thread, &ctxt);
	}
	for (i = 0; i < 8; i++) {
		if (!g_keep_stressing_flag)
			break;
		stress_sys_rw(args, path, ctxt.badbuf);
	}
	keep_running = false;

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}

	if (ctxt.badbuf)
		munmap(ctxt.badbuf, SYS_BUF_SZ);
}

/*
 *  stress_sys_dir()
 *	read directory
 */
static void stress_sys_dir(
	const args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	bool sys_rw)
{
	DIR *dp;
	struct dirent *d;

	if (!g_keep_stressing_flag)
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char filename[PATH_MAX];

		if (!g_keep_stressing_flag)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				(void)snprintf(filename, sizeof(filename),
					"%s/%s", path, d->d_name);
				stress_sys_dir(args, filename, recurse,
					depth + 1, sys_rw);
			}
			break;
		case DT_REG:
			if (sys_rw) {
				(void)snprintf(filename, sizeof(filename),
					"%s/%s", path, d->d_name);
				stress_sys_rw_threads(args, filename);
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
int stress_sysfs(const args_t *args)
{
	bool sys_rw = true;

	if (geteuid() == 0) {
		if (args->instance == 0) {
			pr_inf("%s: running as root, just traversing /sys "
				"and not read/writing to /sys files.\n", args->name);
		}
		sys_rw = false;
	}

	do {
		stress_sys_dir(args, "/sys", true, 0, sys_rw);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_sysfs(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
