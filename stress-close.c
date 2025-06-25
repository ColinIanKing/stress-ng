/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-capabilities.h"
#include "core-pthread.h"
#include <sys/socket.h>

#if defined(__NR_userfaultfd)
#define HAVE_USERFAULTFD
#endif

#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif

#if defined(HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif

#if defined(HAVE_SYS_FANOTIFY_H)
#include <sys/fanotify.h>
#endif

#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif

#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#endif

#if defined(HAVE_SYS_SIGNALFD_H)
#include <sys/signalfd.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"close N",	"start N workers that exercise races on close" },
	{ NULL,	"close-ops N",	"stop after N bogo close operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_PTHREAD)

#define MAX_PTHREADS	(3)
#define SHM_NAME_LEN	(128)
#define FDS_START	(1024)
#define FDS_TO_DUP	(8)

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
#if defined(AF_INET)
	AF_INET,
#endif
#if defined(AF_INET6)
	AF_INET6,
#endif
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
/*
#if defined(AF_APPLETALK)
	AF_APPLETALK,
#endif
*/
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

static const int close_range_flags[] = {
#if defined(CLOSE_RANGE_UNSHARE)
	CLOSE_RANGE_UNSHARE,
#endif
#if defined(CLOSE_RANGE_CLOEXEC)
	CLOSE_RANGE_CLOEXEC,
#endif
	0
};

/*
 *  stress_close_func()
 *	pthread that close file descriptors
 */
static void *stress_close_func(void *arg)
{
	stress_pthread_args_t *pargs = (stress_pthread_args_t *)arg;
	stress_args_t *args = pargs->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
#if !defined(__APPLE__) &&	\
    !defined(__DragonFly__)
	(void)sigprocmask(SIG_BLOCK, &set, NULL);
#endif

	stress_random_small_sleep();

	while (stress_continue(args)) {
		const uint64_t delay =
			max_delay_us ? stress_mwc64modn(max_delay_us) : 0;
		int fds[FDS_TO_DUP], i, ret;
		int flag;

		for (i = 0; i < FDS_TO_DUP; i++) {
			fds[i] = dup2(fileno(stderr), i + FDS_START);
		}

		(void)shim_usleep_interruptible(delay);
		if (fd != -1)
			(void)close(fd);
		if (dupfd != -1)
			(void)close(dupfd);

#if defined(F_GETFL)
		{
			const int fd_rnd = (int)stress_mwc32() + 64;

			/*
			 *  close random unused fd to force EBADF
			 */
			if (fcntl((int)fd_rnd, F_GETFL) == -1)
				(void)close(fd_rnd);
		}
#else
		UNEXPECTED
#endif
		/*
		 *  close a range of fds
		 */
		flag = close_range_flags[stress_mwc8modn(SIZEOF_ARRAY(close_range_flags))];
		ret = shim_close_range(FDS_START, FDS_START + FDS_TO_DUP, flag);
		if ((ret < 0) || (errno == ENOSYS)) {
			for (i = 0; i < FDS_TO_DUP; i++)
				(void)close(fds[i]);
		}
		/*
		 *  close an invalid range of fds
		 */
		VOID_RET(int, shim_close_range(FDS_START + FDS_TO_DUP, FDS_START, 0));

		/*
		 *  close with invalid fds and flags
		 */
		VOID_RET(int, shim_close_range(FDS_START + FDS_TO_DUP, FDS_START, ~0U));
	}

	return &g_nowt;
}

/*
 *  stress_close()
 *	stress by closing file descriptors
 */
static int stress_close(stress_args_t *args)
{
	stress_pthread_args_t pargs;
	pthread_t pthread[MAX_PTHREADS];
	int rc = EXIT_NO_RESOURCE;
	int ret, rets[MAX_PTHREADS];
	const int bad_fd = stress_get_bad_fd();
	size_t i;
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	const bool not_root = !stress_check_capability(SHIM_CAP_IS_ROOT);
	bool close_failure = false;
	double max_duration = 0.0, duration = 0.0, count = 0.0, rate;
#if defined(HAVE_FACCESSAT)
	int file_fd = -1;
	char filename[PATH_MAX];
#endif
#if defined(HAVE_LIB_RT)
	char shm_name[SHM_NAME_LEN];
#endif

#if defined(HAVE_LIB_RT)
	(void)snprintf(shm_name, SHM_NAME_LEN,
		"stress-ng-%d-%" PRIx32, (int)getpid(), stress_mwc32());
#endif

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

#if defined(HAVE_FACCESSAT)
	{
		ret = stress_temp_dir_mk_args(args);
		if (ret < 0)
			return stress_exit_status(-ret);
		(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
		file_fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (file_fd < 0) {
			pr_err("%s: cannot create %s\n", args->name, filename);
			return stress_exit_status(errno);
		}
		(void)shim_unlink(filename);
	}
#else
	UNEXPECTED
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t domain, type;
		int pipefds[2];
		struct stat statbuf;
		double t1, t2, dt;

		fd = -1;
		t1 = stress_time_now();

		switch (stress_mwc8() & 0x0f) {
		case 0:
			domain = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(domains));
			type = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(types));
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
			if (LIKELY(pipe(pipefds) == 0)) {
				fd = pipefds[0];
				pipefds[0] = -1;
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
#if defined(O_PATH)
		case 10:
			fd = open("/tmp", O_PATH | O_RDWR);
			break;
#endif
#if defined(O_DIRECTORY) &&	\
    defined(O_CLOEXEC)
		case 11:
			fd = open("/tmp/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			break;
#endif
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_SHM_OPEN) &&	\
    defined(HAVE_SHM_UNLINK)
		case 12:
			fd = shm_open(shm_name, O_CREAT | O_RDWR | O_TRUNC,
				S_IRUSR | S_IWUSR);
			(void)shm_unlink(shm_name);
			break;
#endif
		case 13:
			fd = bad_fd;
			break;
		case 14:
#if defined(__linux__)
			fd = open("/proc/self/fd/0", O_RDONLY);
#else
			fd = dup(0);
#endif
			break;
		case 15:
			fd = open("/dev/stdin", O_RDONLY);
			break;
		default:
			fd = open(".", O_RDONLY | O_DIRECTORY);
			break;
		}
		if (UNLIKELY(fd == -1))
			fd = open("/dev/null", O_RDWR);

		if (LIKELY(fd != -1)) {
			double t;

			dupfd = dup(fd);
			if (not_root) {
#if defined(HAVE_FCHOWNAT)
				VOID_RET(int, fchownat(fd, "", uid, gid, 0));

				VOID_RET(int, fchownat(fd, "", uid, gid, ~0));
#endif
				VOID_RET(int, fchown(fd, uid, gid));
			}
#if defined(HAVE_FACCESSAT)
			VOID_RET(int, faccessat(fd, "", F_OK, 0));

			/*
			 * Exercise bad dirfd resulting in Error EBADF
			 */
			VOID_RET(int, faccessat(bad_fd, "", F_OK, 0));

			/*
			 * Exercise invalid flags syscall
			 */
			VOID_RET(int, faccessat(fd, "", ~0, 0));

			/*
			 * Invalid faccessat syscall with pathname is relative and dirfd
			 * is a file descriptor referring to a file other than a directory
			 */
			ret = faccessat(file_fd, "./", F_OK, 0);
			if (UNLIKELY(ret >= 0)) {
				pr_fail("%s: faccessat opened file descriptor succeeded unexpectedly, "
					"errno=%d (%s)\n", args->name, errno, strerror(errno));
				(void)close(fd);
				if (dupfd != -1)
					(void)close(dupfd);
				rc = EXIT_FAILURE;
				goto tidy;
			}
#else
			UNEXPECTED
#endif
			VOID_RET(int, shim_fstat(fd, &statbuf));

			ret = dup(STDOUT_FILENO);
			if (LIKELY(ret != -1))
				(void)close(ret);

			t = stress_time_now();
			if (LIKELY(close(fd) == 0)) {
				duration += stress_time_now() - t;
				count += 1.0;

				/*
				 *  A close on a successfully closed fd should fail
				 */
				if (UNLIKELY(close(fd) == 0)) {
					pr_fail("%s: unexpectedly able to close the same file %d twice\n",
						args->name, fd);
					close_failure = true;
				} else {
					if (UNLIKELY((errno != EBADF) &&
						     (errno != EINTR))) {
						pr_fail("%s: expected error on close failure, error=%d (%s)\n",
							args->name,  errno, strerror(errno));
						close_failure = true;
					}
				}
			}
			if (dupfd != -1)
				(void)close(dupfd);
		}
		t2 = stress_time_now();
		dt = t2 - t1;
		if (dt > max_duration) {
			max_duration = dt ;
			/* max delay is 75% of the duration in microseconds */
			max_delay_us = (uint64_t)(duration * 750000);
		}

		max_duration *= 0.995;
		if (max_duration < 1.0)
			max_duration = 1.0;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = close_failure ? EXIT_FAILURE : EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "close calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	for (i = 0; i < MAX_PTHREADS; i++) {
		if (rets[i] == -1)
			continue;
		ret = pthread_join(pthread[i], NULL);
		if ((ret) && (ret != ESRCH)) {
			pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
				args->name, ret, strerror(ret));
		}
	}

#if defined(HAVE_FACCESSAT)
	if (file_fd >= 0)
		(void)close(file_fd);
	(void)stress_temp_dir_rm_args(args);
#else
	UNEXPECTED
#endif

	return rc;
}

const stressor_info_t stress_close_info = {
	.stressor = stress_close,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_close_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
