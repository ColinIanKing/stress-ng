/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#if defined(HAVE_LIB_PTHREAD) && !defined(__sun__)

#include <poll.h>
#include <termios.h>

#if defined(HAVE_LINUX_MEDIA_H)
#include <linux/media.h>
#endif
#if defined(HAVE_LINUX_VT_H)
#include <linux/vt.h>
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
#include <linux/dm-ioctl.h>
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
#include <linux/videodev2.h>
#endif

#define MAX_DEV_THREADS		(4)

static volatile bool keep_running;
static sigset_t set;

typedef struct {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const char *name, const int fd, const char *devpath);
} dev_func_t;

#if defined(HAVE_LINUX_MEDIA_H)
static void stress_dev_media_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(MEDIA_IOC_DEVICE_INFO)
	{
		struct media_device_info mdi;
		int ret;

		ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
		if (ret < 0)
			return;

		if (!mdi.driver[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null driver name\n",
				name, devpath);
		if (!mdi.model[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null model name\n",
				name, devpath);
		if (!mdi.bus_info[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null bus_info field\n",
				name, devpath);
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VT_H)
static void stress_dev_vcs_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VT_GETMODE)
	{
		struct vt_mode mode;
		int ret;

		ret = ioctl(fd, VT_GETMODE, &mode);
		(void)ret;
	}
#endif
#if defined(VT_GETSTATE)
	{
		struct vt_stat  stat;
		int ret;

		ret = ioctl(fd, VT_GETSTATE, &stat);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
static void stress_dev_dm_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(DM_VERSION)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_VERSION, &dm);
		(void)ret;
	}
#endif
#if defined(DM_STATUS)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_STATUS, &dm);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
static void stress_dev_video_linux(const char *name, const int fd, const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VIDIOC_QUERYCAP)
	{
		struct v4l2_capability c;
		int ret;

		ret = ioctl(fd, VIDIOC_QUERYCAP, &c);
		(void)ret;
	}
#endif
}
#endif

static void stress_dev_tty(const char *name, const int fd, const char *devpath)
{
	struct termios t;
	int ret;

	(void)name;
	(void)devpath;

	ret = tcgetattr(fd, &t);
	(void)ret;
#if defined(TCGETS)
	ret = ioctl(fd, TCGETS, &t);
	(void)ret;
#endif
}

#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const dev_func_t dev_funcs[] = {
#if defined(__linux__) && defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#endif
#if defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/dm",	stress_dev_dm_linux),
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
#endif
	DEV_FUNC("/dev/tty",	stress_dev_tty),
};

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
	fd_set rfds, wfds;
	void *ptr;
	struct timeval tv;
	size_t i;

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		goto rdwr;

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

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	FD_ZERO(&wfds);
	FD_SET(fd, &wfds);
	tv.tv_sec = 0;
	tv.tv_usec = 10000;
	ret = select(fd + 1, &rfds, &wfds, NULL, &tv);
	(void)ret;

#if defined(F_GETFD)
	ret = fcntl(fd, F_GETFD, NULL);
	(void)ret;
#endif
#if defined(F_GETFL)
	ret = fcntl(fd, F_GETFL, NULL);
	(void)ret;
#endif
#if defined(F_GETSIG)
	ret = fcntl(fd, F_GETSIG, NULL);
	(void)ret;
#endif
	ptr = mmap(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, args->page_size);
	(void)close(fd);

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		goto sync;
	ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, args->page_size);

sync:
	ret = fsync(fd);
	(void)ret;

	for (i = 0; i < SIZEOF_ARRAY(dev_funcs); i++) {
		if (!strncmp(path, dev_funcs[i].devpath, dev_funcs[i].devpath_len))
			dev_funcs[i].func(args->name, fd, path);
	}

	(void)close(fd);

rdwr:
	/*
	 *   O_RDONLY | O_WRONLY allows one to
	 *   use the fd for ioctl() only operations
	 */
	fd = open(path, O_RDONLY | O_WRONLY | O_NONBLOCK);
	if (fd >= 0) {
		(void)close(fd);
	}
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
 *  stress_dev_threads()
 *	create a bunch of threads to thrash dev entries
 */
static void stress_dev_threads(const args_t *args, char *path)
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
	const int depth,
	const uid_t euid)
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

		if (!keep_stressing())
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		/* Xen clients hang on hpet when running as root */
		if (!euid && !strcmp(d->d_name, "hpet"))
			continue;

		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				inc_counter(args);
				(void)snprintf(filename, sizeof(filename),
					"%s/%s", path, d->d_name);
				stress_dev_dir(args, filename, recurse,
					depth + 1, euid);
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
	uid_t euid = geteuid();

	do {
		pid_t pid;

again:
		if (!keep_stressing())
			break;
		pid = fork();
		if (pid < 0) {
			if (errno == EAGAIN)
				goto again;
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
			}
		} else if (pid == 0) {
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			/* Make sure this is killable by OOM killer */
			set_oom_adjustment(args->name, true);
			stress_dev_dir(args, "/dev", true, 0, euid);
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_dev(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
