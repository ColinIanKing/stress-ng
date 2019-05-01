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
	{ NULL,	"close N",	"start N workers that exercise races on close" },
	{ NULL,	"close-ops N",	"stop after N bogo close operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD)

#define MAX_PTHREADS	(3)

static volatile int fd, dupfd;
static volatile uint64_t max_delay_us = 1;
static sigset_t set;

static const int domains[] = {
#if defined(AF_UNIX)
	AF_UNIX,
#endif
#if defined(AF_LOCAL)
	AF_LOCAL,
#endif
/*
#if defined(AF_INET)
	AF_INET,
#endif
#if defined(AF_INET6)
	AF_INET6,
#endif
*/
/*
#if defined(AF_IPX)
	AF_IPX,
#endif
#if defined(AF_NETLINK)
	AF_NETLINK,
#endif
#if defined(AF_X25)
	AF_X25,
#endif
#if defined(AF_AX25)
	AF_AX25,
#endif
#if defined(AF_ATMPVC)
	AF_ATMPVC,
#endif
*/
#if defined(AF_APPLETALK)
	AF_APPLETALK,
#endif
#if defined(AF_PACKET)
	AF_PACKET,
#endif
#if defined(AF_ALG)
	AF_ALG,
#endif
	0,
};

static const int types[] = {
#if defined(SOCK_STREAM)
	SOCK_STREAM,
#endif
#if defined(SOCK_DGRAM)
	SOCK_DGRAM,
#endif
#if defined(SOCK_SEQPACKET)
	SOCK_SEQPACKET,
#endif
#if defined(SOCK_RAW)
	SOCK_RAW,
#endif
#if defined(SOCK_RDM)
	SOCK_RDM,
#endif
	0,
};

/*
 *  stress_close_func()
 *	pthread that exits immediately
 */
static void *stress_close_func(void *arg)
{
	static void *nowt = NULL;
	pthread_args_t *pargs = (pthread_args_t *)arg;
	const args_t *args = pargs->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
#if !defined(__APPLE__) && !defined(__DragonFly__)
	(void)sigprocmask(SIG_BLOCK, &set, NULL);
#endif

	while (keep_stressing()) {
		shim_usleep_interruptible(mwc32() % max_delay_us);
		(void)close(fd);
		shim_usleep_interruptible(mwc32() % max_delay_us);
		(void)close(dupfd);
	}

	return &nowt;
}

/*
 *  stress_close()
 *	stress by creating pthreads
 */
static int stress_close(const args_t *args)
{
	pthread_args_t pargs;
	pthread_t pthread[MAX_PTHREADS];
	int rc = EXIT_NO_RESOURCE;
	int ret, rets[MAX_PTHREADS];
	size_t i;
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	double max_duration = 0.0;
	(void)sigfillset(&set);

	fd = -1;
	dupfd = -1;

	pargs.args = args;
	pargs.data = NULL;

	for (i = 0; i < MAX_PTHREADS; i++)
		rets[i] = -1;

	for (i = 0; i < MAX_PTHREADS; i++) {
		rets[i] = pthread_create(&pthread[i], NULL, stress_close_func, (void *)&pargs);
		if (rets[i]) {
			pr_inf("%s: failed to create a pthread, error=%d (%s)\n",
				args->name, rets[i], strerror(rets[i]));
			goto tidy;
		}
	}

	do {
		size_t domain, type;
		int pipefds[2];
		struct stat statbuf;
		fd = -1;
		double t1, t2, duration;

		t1 = time_now();

		switch (mwc8() % 10) {
		case 0:
			domain = mwc8() % SIZEOF_ARRAY(domains);
			type = mwc8() % SIZEOF_ARRAY(types);
			fd = socket(domains[domain], types[type], 0);
			break;
		case 1:
			fd = open("/dev/zero", O_RDONLY);
			break;
#if defined(O_TMPFILE)
		case 2:
			fd = open("/tmp", O_TMPFILE | O_RDWR,
					S_IRUSR | S_IWUSR);
			break;
#endif
#if defined(HAVE_SYS_EPOLL_H)
		case 3:
			fd = epoll_create(1);
			break;
#endif
#if defined(HAVE_SYS_EVENTFD_H) &&	\
    defined(HAVE_EVENTFD) &&		\
    NEED_GLIBC(2,8,0)
		case 4:
			fd = eventfd(0, 0);
			break;
#endif
#if defined(HAVE_SYS_FANOTIFY_H) &&	\
    defined(HAVE_FANOTIFY)
		case 5:
			fd = fanotify_init(0, 0);
			break;
#endif
#if defined(HAVE_INOTIFY) &&		\
    defined(HAVE_SYS_INOTIFY_H)
		case 6:
			fd = inotify_init();
			break;
#endif
		case 7:
			if (pipe(pipefds) == 0) {
				fd = pipefds[0];
				(void)close(pipefds[1]);
			}
			break;
#if defined(HAVE_SYS_SIGNALFD_H) &&     \
    NEED_GLIBC(2,8,0) &&                \
    defined(HAVE_SIGQUEUE) &&		\
    defined(SIGRTMIN)
		case 8:
			{
				sigset_t mask;

				(void)sigemptyset(&mask);
				(void)sigaddset(&mask, SIGRTMIN);
				fd = signalfd(-1, &mask, 0);
			}
			break;
#endif
#if defined(HAVE_USERFAULTFD) &&	\
    defined(HAVE_LINUX_USERFAULTFD_H)
		case 9:
			fd = shim_userfaultfd(0);
			break;
#endif
		default:
			break;
		}
		if (fd == -1)
			fd = open("/dev/null", O_RDWR);

		if (fd != -1) {
			dupfd = dup(fd);
#if defined(HAVE_FCHOWNAT)
			ret = fchownat(fd, "", uid, gid, 0);
			(void)ret;
#endif
			ret = fchown(fd, uid, gid);
			(void)ret;
#if defined(HAVE_FACCESSAT)
			ret = faccessat(fd, "", F_OK, 0);
			(void)ret;
#endif
			ret = fstat(fd, &statbuf);
			(void)ret;

			(void)close(fd);
			if (dupfd != -1)
				(void)close(dupfd);
		}
		t2 = time_now();
		duration = t2 - t1;
		if (duration > max_duration) {
			max_duration = duration;
			/* max delay is 75% of the duration in microseconds */
			max_delay_us = duration * 750000;
		}

		max_duration *= 0.995;
		if (max_duration < 1.0)
			max_duration = 1.0;
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
tidy:

	for (i = 0; i < MAX_PTHREADS; i++) {
		if (rets[i] == -1)
			continue;
		ret = pthread_join(pthread[i], NULL);
		if ((ret) && (ret != ESRCH)) {
			pr_fail("%s: pthread_join failed (parent), errno=%d (%s)",
				args->name, ret, strerror(ret));
		}
	}

	return rc;
}

stressor_info_t stress_close_info = {
	.stressor = stress_close,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_close_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
