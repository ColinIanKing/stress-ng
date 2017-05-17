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

#include <poll.h>

#define MAX_DEV_THREADS		(4)

static volatile bool keep_running;
static sigset_t set;

/*
 *  stress_dev_rw()
 *	exercise a dev entry
 */
static inline void stress_dev_rw(
	const args_t *args,
	const char *path)
{
	int fd, ret;
	off_t off;
	struct stat buf;
	struct pollfd fds[1];
	fd_set rfds;
	void *ptr;

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;

	if (fstat(fd, &buf) < 0) {
		pr_fail_err("stat");
	} else {
		if ((buf.st_mode & (S_IFBLK | S_IFCHR)) == 0) {
			pr_fail("%s: device entry '%s' is not "
				"a block or char device\n",
			args->name, path);
		}
	}
	off = lseek(fd, 0, SEEK_SET);
	(void)off;

	FD_ZERO(&rfds);
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	ret = poll(fds, 1, 0);
	(void)ret;

#if defined(F_GETFD)
	ret = fcntl(fd, F_GETFD, NULL);
	(void)ret;
#endif
#if defined(F_GETFL)
	ret = fcntl(fd, F_GETFL, NULL);
	(void)ret;
#endif
	ptr = mmap(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, args->page_size);
	(void)close(fd);

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;
	ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, args->page_size);

	ret = fsync(fd);
	(void)ret;

	(void)close(fd);
}

/*
 *  stress_dev_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_dev_thread(void *arg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	pthread_args_t *pa = (pthread_args_t *)arg;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	while (keep_running && g_keep_stressing_flag)
		stress_dev_rw(pa->args, (const char *)pa->data);

	return &nowt;
}

/*
 *  stress_proc_sys_threads()
 *	create a bunch of threads to thrash read a sys entry
 */
static void stress_dev_threads(const args_t *args, const char *path)
{
	size_t i;
	pthread_t pthreads[MAX_DEV_THREADS];
	int ret[MAX_DEV_THREADS];
	pthread_args_t pa;

	pa.args = args;
	pa.data = (void *)path;

	(void)memset(ret, 0, sizeof(ret));

	keep_running = true;

	for (i = 0; i < MAX_DEV_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_dev_thread, &pa);
	}
	for (i = 0; i < 8; i++) {
		if (!g_keep_stressing_flag)
			break;
		stress_dev_rw(args, path);
	}
	keep_running = false;

	for (i = 0; i < MAX_DEV_THREADS; i++) {
		if (ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}
}

/*
 *  stress_dev_dir()
 *	read directory
 */
static void stress_dev_dir(
	const args_t *args,
	const char *path,
	const bool recurse,
	const int depth)
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
				stress_dev_dir(args, filename, recurse,
					depth + 1);
			}
			break;
		case DT_BLK:
		case DT_CHR:
			(void)snprintf(filename, sizeof(filename),
				"%s/%s", path, d->d_name);
			if (!strstr(filename, "watchdog"))
				stress_dev_threads(args, filename);
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  stress_dev
 *	stress reading all of /dev
 */
int stress_dev(const args_t *args)
{
	do {
		stress_dev_dir(args, "/dev", true, 0);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_dev(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
