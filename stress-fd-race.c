/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-net.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"

#include <sys/ioctl.h>
#include <ctype.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#define DEFAULT_SOCKET_FD_PORT	(15000)
#define MAX_PTHREADS		(4)

/* list entry of filename and open flags */
typedef struct stress_fd_race_filename {
	struct stress_fd_race_filename *next;
	char *filename;		/* filename */
	int flags;		/* open flags  */
} stress_fd_race_filename_t;

static const stress_help_t help[] = {
	{ NULL,	"fd-race N",	 "start N workers sending file descriptors over sockets" },
	{ NULL,	"fd-race-ops N", "stop after N fd_race bogo operations" },
	{ NULL, "fd-race-dev",	 "race on /dev/* files" },
	{ NULL, "fd-race-proc",	 "race on /proc/* files" },
	{ NULL,	NULL,		 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_fd_race_dev,  "fd-race-dev",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_fd_race_proc, "fd-race-proc", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__linux__) &&		\
    defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_PTHREAD_BARRIER)

#define MSG_ID			'M'

/* stressor context for multiple child processes and pthreads */
typedef struct {
	stress_args_t *args;		/* stressor args */
	pid_t pid;			/* stressor's pid */
	ssize_t max_fd;			/* maximum fd allowed */
	int socket_fd_port;		/* socket fd port */
	size_t fds_size;		/* size of fd array in bytes */
	int *fds;			/* pointer to fd array */
	int n;				/* elements in fd array */
	pthread_barrier_t barrier;	/* pthread sync barrier */
	dev_t proc_dev;			/* /proc dev number */
	dev_t dev_dev;			/* /dev dev number */
	volatile int current_fd;	/* current fd being opened */
} stress_fd_race_context;

static int stress_fd_race_close_range_flag(void)
{
#if defined(CLOSE_RANGE_UNSHARE)
	return stress_mwc1() ? CLOSE_RANGE_UNSHARE : 0;
#else
	return 0;
#endif
}

/*
 *  stress_fd_race_close_fds()
 *	close fds, use randomly chosen closing, either
 *	by close_range, forward, reverse or randomly
 *	ordered fds
 */
static void stress_fd_race_close_fds(
	int *fds,
	const size_t n,
	const int fds_min,
	const int fds_max,
	const int flag)
{
	size_t i;

	if (!fds)
		return;
	if (n < 1)
		return;
	if ((fds_min == INT_MAX) || (fds_max < 0))
		return;
	if (stress_mwc1()) {
		/* Try to close on a range */
		if (shim_close_range(fds_min, fds_max, flag) == 0)
			return;
	}
	switch (stress_mwc8modn(4)) {
	case 0:
	default:
		/* Close fds in order */
		for (i = 0; i < n; i++)
			(void)close(fds[i]);
		break;
	case 1:
		/* Close fds in reverse order */
		for (i = n; i > 0; i--)
			(void)close(fds[i - 1]);
		break;
	case 2:
		/* Close fds with stride */
		for (i = 0; i < n; i += 2)
			(void)close(fds[i]);
		for (i = 1; i < n; i += 2)
			(void)close(fds[i]);
		break;
	case 3:
		/* Close fds in randomized order */
		for (i = 0; i < n; i++) {
			register size_t j;
			register int tmp;

			j = (size_t)stress_mwc32modn(n);

			tmp = fds[i];
			fds[j] = fds[i];
			fds[i] = tmp;
		}
		for (i = 0; i < n; i++)
			(void)close(fds[i]);
		break;
	}
}

/*
 *  stress_race_fd_send()
 *	send a fd (fd_send) over a socket fd
 */
static inline ssize_t stress_race_fd_send(const int fd, const int fd_send)
{
	struct iovec iov;
	struct msghdr msg ALIGN64;
	struct cmsghdr *cmsg;
	int *ptr;

	char ctrl[CMSG_SPACE(sizeof(int))];
	static char msg_data[1] = { MSG_ID };

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)shim_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)shim_memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	ptr = (int *)(uintptr_t)CMSG_DATA(cmsg);
	*ptr = fd_send;
	return sendmsg(fd, &msg, 0);
}

/*
 *  stress_race_fd_recv()
 *	recv an fd over a socket, return fd or -1 if fail
 */
static inline int stress_race_fd_recv(const int fd)
{
	struct iovec iov;
	struct msghdr ALIGN64 msg;
	struct cmsghdr *cmsg;
	char msg_data[1] = { 0 };
	char ctrl[CMSG_SPACE(sizeof(int))];

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)shim_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)shim_memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	if (UNLIKELY(recvmsg(fd, &msg, 0) <= 0))
		return -1;
	if (UNLIKELY(msg_data[0] != MSG_ID))
		return -1;
	if (UNLIKELY((msg.msg_flags & MSG_CTRUNC) == MSG_CTRUNC))
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg &&
	    (cmsg->cmsg_level == SOL_SOCKET) &&
	    (cmsg->cmsg_type == SCM_RIGHTS) &&
	    ((size_t)cmsg->cmsg_len >= (size_t)CMSG_LEN(sizeof(int)))) {
		int *const ptr = (int *)(uintptr_t)CMSG_DATA(cmsg);
		return *ptr;
	}

	return -1;
}

/*
 *  stress_fd_race_pthread()
 *	if fd is writable produce some pending writes and then
 *	exit the pthread, the exit will force close the opened fds
 *	in process clean-up
 */
static void *stress_fd_race_pthread(void *ptr)
{
	stress_fd_race_context *context = (stress_fd_race_context *)ptr;

	int i;

	stress_random_small_sleep();

	for (i = 0; i < context->n; i++) {
		if (context->fds[i] > 0) {
			struct stat statbuf;

			if (fstat(context->fds[i], &statbuf) == 0) {
				if ((statbuf.st_dev != context->proc_dev) &&
				    (statbuf.st_dev != context->dev_dev) &&
				    ((statbuf.st_mode & S_IFMT) ==  S_IFREG)) {
					VOID_RET(ssize_t, write(context->fds[i], &i, sizeof(i)));
				}
			}
		}
	}

	(void)pthread_barrier_wait(&context->barrier);

	/* termination of pthread will close fds */
	return &g_nowt;
}

/*
 *  stress_race_fd_client()
 *	client reader
 */
static int OPTIMIZE3 stress_race_fd_client(stress_fd_race_context *context)
{
	struct sockaddr *addr = NULL;
	stress_args_t *args = context->args;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		ssize_t n;
		socklen_t addr_len = 0;
		int i, fd, retries = 0, so_reuseaddr = 1;
		int fds_min = INT_MAX, fds_max = -1;
		int pthreads_ret[MAX_PTHREADS];
		pthread_t pthreads[MAX_PTHREADS];

		(void)shim_memset(pthreads_ret, 0, sizeof(pthreads_ret));
		(void)shim_memset(pthreads, 0, sizeof(pthreads));

		(void)shim_memset(context->fds, 0, context->fds_size);
retry:
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_NO_RESOURCE;

		if (UNLIKELY((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
			if ((errno == ENFILE) ||
			    (errno == ENOBUFS) ||
			    (errno == ENOMEM)) {
				stress_random_small_sleep();
				goto retry;
			}
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (UNLIKELY(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
				&so_reuseaddr, sizeof(so_reuseaddr)) < 0)) {
			(void)close(fd);
			pr_fail("%s: setsockopt SO_REUSEADDR failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if (UNLIKELY(stress_set_sockaddr(args->name, args->instance, context->pid,
				AF_UNIX, context->socket_fd_port,
				&addr, &addr_len, NET_ADDR_ANY) < 0)) {
			return EXIT_FAILURE;
		}
		if (UNLIKELY(connect(fd, addr, addr_len) < 0)) {
			(void)close(fd);
			if (retries++ > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_NO_RESOURCE;
			}
			(void)shim_usleep(10000);
			goto retry;
		}

		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;

		for (n = 0; LIKELY(stress_continue(args) && (n < context->max_fd)); n++) {
			context->fds[n] = stress_race_fd_recv(fd);
			if (context->fds[n] < 0)
				continue;
			if (fds_max < context->fds[n])
				fds_max = context->fds[n];
			if (fds_min > context->fds[n])
				fds_min = context->fds[n];
		}
		context->n = n;

		for (i = 0; i < MAX_PTHREADS; i++) {
			pthreads_ret[i] = pthread_create(&pthreads[i], NULL, stress_fd_race_pthread, context);
		}
		for (i = 0; i < MAX_PTHREADS; i++) {
			if (pthreads_ret[i] == 0)
				(void)pthread_join(pthreads[i], NULL);
		}

		stress_fd_race_close_fds(context->fds, n, fds_min, fds_max, stress_fd_race_close_range_flag());
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (stress_continue(args));

#if defined(HAVE_SOCKADDR_UN)
	if (addr) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_fd_race_current()
 *	exercise next set of 10 fds that may be opened on the
 *	server process to force races on the fds during their
 *	opening phase in kernel space
 */
static void *stress_fd_race_current(void *ptr)
{
	stress_fd_race_context *context = (stress_fd_race_context *)ptr;
	stress_args_t *args = context->args;

	do {
		int current_fd = context->current_fd;
		int fd = current_fd + stress_mwc1();
		const int fd_end = fd + 10;

		if (context->current_fd == -1) {
			(void)shim_usleep(200000);
			continue;
		}

		while (fd < fd_end) {
			struct stat statbuf;
			uint8_t rnd;
#if defined(FIONREAD)
			int isz;
#endif
			int fdup;
#if defined(HAVE_POLL_H)
			struct pollfd pfds[1];
#endif

			rnd = stress_mwc8modn(11);
			switch (rnd) {
			case 0:
				fdup = dup(fd);
				if (fdup >= 0)
					(void)close(fdup);
				break;
			case 1:
				VOID_RET(int, fstat(fd, &statbuf));
				break;
			case 2:
				VOID_RET(int, shim_fsync(fd));
				break;
			case 3:
				VOID_RET(off_t, lseek(fd, 0, SEEK_SET));
				break;
			case 4:
				VOID_RET(int, fcntl(fd, F_GETFL, NULL));
				break;
			case 5:
				VOID_RET(int, shim_fdatasync(fd));
				break;
#if defined(HAVE_POSIX_FADVISE) && 	\
    defined(POSIX_FADV_NORMAL)
			case 6:
				VOID_RET(int, posix_fadvise(fd, 0, 1024, POSIX_FADV_NORMAL));
				break;
#endif
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_UN)
			case 7:
				VOID_RET(int, flock(fd, LOCK_UN));
				break;
#endif
#if defined(FIONREAD)
			case 8:
				VOID_RET(int, ioctl(fd, FIONREAD, &isz));
				break;
#endif
#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
			case 9:
				pfds[0].fd = fd;
				pfds[0].events = POLLIN | POLLOUT;
				pfds[0].revents = 0;
				VOID_RET(int, poll(pfds, 1, 0));
				break;
#endif
#if defined(HAVE_SYS_SELECT_H) &&       \
    defined(HAVE_SELECT)
			case 10:
				if (fd < FD_SETSIZE) {
					fd_set rdfds, wrfds;
					struct timeval timeout;

					timeout.tv_sec = 0;
					timeout.tv_usec = 1;

					FD_ZERO(&rdfds);
					FD_SET(fd, &rdfds);
					FD_ZERO(&wrfds);
					FD_SET(fd, &wrfds);

					VOID_RET(int, select(fd + 1, &rdfds, &wrfds, NULL, &timeout));
				}
				break;
#endif
			default:
				break;
			}
			fd++;
		}
	} while (stress_continue(args));

	return &g_nowt;
}


/*
 *  stress_race_fd_server()
 *	server writer
 */
static int OPTIMIZE3 stress_race_fd_server(
	stress_fd_race_context *context,
	stress_fd_race_filename_t *list)
{
	size_t j;
	int fd, so_reuseaddr = 1, rc = EXIT_SUCCESS;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	stress_fd_race_filename_t *entry;
	stress_args_t *args = context->args;
	int pthreads_ret[MAX_PTHREADS];
	pthread_t pthreads[MAX_PTHREADS];

	(void)shim_memset(pthreads_ret, 0, sizeof(pthreads_ret));
	(void)shim_memset(pthreads, 0, sizeof(pthreads));

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}

retry:
	if (UNLIKELY(!stress_continue_flag()))
		goto die;
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		if ((errno == ENFILE) ||
		    (errno == ENOBUFS) ||
		    (errno == ENOMEM)) {
			stress_random_small_sleep();
			goto retry;
		}
		rc = stress_exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (stress_set_sockaddr(args->name, args->instance, context->pid,
			AF_UNIX, context->socket_fd_port,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (bind(fd, addr, addr_len) < 0) {
		if (errno == EADDRINUSE) {
			rc = EXIT_NO_RESOURCE;
			pr_inf_skip("%s: cannot bind, skipping stressor, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto die_close;
		}
		rc = stress_exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail("%s: listen failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	for (j = 0; j < MAX_PTHREADS; j++) {
		pthreads_ret[j] = pthread_create(&pthreads[j], NULL, stress_fd_race_current, context);
	}

	entry = list;
	do {
		int sfd;

		if (UNLIKELY(!stress_continue(args)))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			ssize_t i;
			int fds_min = INT_MAX, fds_max = -1;
			double t_end = stress_time_now() + 0.5;

			(void)shim_memset(context->fds, 0, context->fds_size);

			for (i = 0; LIKELY(stress_continue(args) && (i < context->max_fd)); i++) {
				context->fds[i] = open(entry->filename, entry->flags);
				context->current_fd = context->fds[i];

				if (context->fds[i] >= 0) {
					ssize_t ret;

					if (context->fds[i] < fds_min)
						fds_min = context->fds[i];
					if (context->fds[i] > fds_max)
						fds_max = context->fds[i];

					ret = stress_race_fd_send(sfd, context->fds[i]);
					if ((ret < 0) &&
					     ((errno != EAGAIN) &&
					      (errno != EINTR) &&
					      (errno != EWOULDBLOCK) &&
					      (errno != ECONNRESET) &&
					      (errno != ENOMEM) &&
#if defined(ETOOMANYREFS)
					      (errno != ETOOMANYREFS) &&
#endif
					      (errno != EPIPE))) {
						pr_fail("%s: sendmsg failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						break;
					}
					msgs++;

					stress_bogo_inc(args);
				}
				if (stress_time_now() > t_end)
					break;
			}
			(void)close(sfd);
			stress_fd_race_close_fds(context->fds, i, fds_min, fds_max, stress_fd_race_close_range_flag());
		}

		/* cycle through filename list */
		entry = entry->next;
		if (!entry)
			entry = list;
	} while (stress_continue(args));

die_close:
	(void)close(fd);
die:
#if defined(HAVE_SOCKADDR_UN)
	if (addr) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	for (j = 0; j < MAX_PTHREADS; j++) {
		if (pthreads_ret[j] == 0) {
			(void)pthread_cancel(pthreads[j]);
			(void)pthread_join(pthreads[j], NULL);
		}
	}
	pr_dbg("%s: %" PRIu64 " file descriptors exercised\n", args->name, msgs);

	return rc;
}

static stress_fd_race_filename_t *stress_fd_race_filename_add(
	stress_fd_race_filename_t **list,
	const char *filename,
	int flags)
{
	stress_fd_race_filename_t *entry;
	size_t i;

	/* Files we don't want to access */
	static const char * const ignore_list[] = {
		"/dev/watchdog",
	};

	for (i = 0; i < SIZEOF_ARRAY(ignore_list); i++) {
		if (strncmp(filename, ignore_list[i], strlen(ignore_list[i])) == 0)
			return NULL;
	}

	entry = (stress_fd_race_filename_t *)malloc(sizeof(*entry));
	if (!entry)
		return NULL;

	entry->filename = shim_strdup(filename);
	if (!entry->filename) {
		free(entry);
		return NULL;
	}
	entry->next = *list;
	entry->flags = flags;
	*list = entry;

	return entry;
}

/*
 *  stress_fd_race_filename_free()
 *	free filename list
 */
static void stress_fd_race_filename_free(stress_fd_race_filename_t *list)
{
	stress_fd_race_filename_t *entry = list;

	while (entry) {
		stress_fd_race_filename_t *next = entry->next;

		free(entry->filename);
		free(entry);
		entry = next;
	};
}

/*
 *  stress_fd_race_filename_dir()
 *	scan top level of a given directory and add
 *	appropriate files to the filename list
 */
static void stress_fd_race_filename_dir(const char *dirname, stress_fd_race_filename_t **list)
{
	DIR *dir;
	struct dirent *de;
	int dir_fd;

	dir_fd = open(dirname, O_DIRECTORY | O_RDONLY);
	if (dir_fd < 0)
		return;

	dir = opendir(dirname);
	if (!dir) {
		(void)close(dir_fd);
		return;
	}

	while ((de = readdir(dir)) != NULL) {
		struct stat statbuf;
		ssize_t len;
		int val;

		/* ignore special dirs */
		if ((de->d_name[0] == '\0') || (de->d_name[0] == '.'))
			continue;

		for (len = (ssize_t)strlen(de->d_name) - 1; len > 1; len--) {
			if (!isdigit((unsigned char)de->d_name[len]))
				break;
		}
		/* allow /dev/tty, /dev/tty0 ignore numbered files such as /dev/tty1 upwards */
		if (sscanf(&de->d_name[len + 1], "%d", &val) == 1)
			if (val > 0)
				continue;
		if (fstatat(dir_fd, de->d_name, &statbuf, 0) < 0)
			continue;

		switch (statbuf.st_mode & S_IFMT) {
		case S_IFBLK:
		case S_IFCHR:
		case S_IFREG:
			if (faccessat(dir_fd, de->d_name, R_OK, 0) == 0) {
				char filename[PATH_MAX];

				(void)snprintf(filename, sizeof(filename), "%s/%s", dirname, de->d_name);
				(void)stress_fd_race_filename_add(list, filename, O_RDONLY);
			}
			break;
		default:
			break;
		}
	}
	(void)closedir(dir);
	(void)close(dir_fd);
}

/*
 *  stress_fd_race_get_dev()
 *	get device number of the given directory
 */
static void stress_fd_race_get_dev(
	stress_args_t *args,
	const char *dirname,
	const char *opt_name,
	dev_t *dev,
	bool *opt_flag)
{
	if (*opt_flag) {
		struct stat statbuf;

		if (stat(dirname, &statbuf) < 0) {
			pr_inf("%s: cannot stat %s, errno=%d (%s), option "
				"%s will be disabled\n", args->name, dirname,
				errno, strerror(errno), opt_name);
			*opt_flag = false;
		} else {
			*dev = statbuf.st_dev;
		}
	}
}

/*
 *  stress_fd_race
 *	stress socket fd passing
 */
static int stress_fd_race(stress_args_t *args)
{
	pid_t pid;
	int fd, rc = EXIT_SUCCESS, ret, reserved_port;
	char filename[PATH_MAX];
	stress_fd_race_filename_t *list = NULL;
	bool fd_race_dev = false;
	bool fd_race_proc = false;
	stress_fd_race_context context;

	(void)shim_memset(&context, 0, sizeof(context));

	context.args = args;
	context.pid = getpid();
	context.max_fd = (ssize_t)stress_get_file_limit();
	context.socket_fd_port = DEFAULT_SOCKET_FD_PORT;
	context.current_fd = -1;

	(void)stress_get_setting("fd-race-dev", &fd_race_dev);
	(void)stress_get_setting("fd-race-proc", &fd_race_proc);

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	stress_fd_race_get_dev(args, "/dev", "fd-race-dev", &context.dev_dev, &fd_race_dev);
	stress_fd_race_get_dev(args, "/proc", "fd-race-proc", &context.proc_dev, &fd_race_proc);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto tidy_dir;
	}
	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf("%s: failed to create file '%s', errno=%d (%s), skipping stressor\n",
			args->name, filename, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_file;
	}
	(void)close(fd);

	if (fd_race_dev)
		stress_fd_race_filename_dir("/dev", &list);
	if (fd_race_proc)
		stress_fd_race_filename_dir("/proc", &list);

	if (!stress_fd_race_filename_add(&list, filename, O_RDWR)) {
		pr_inf("%s: failed to add filename to list, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_file;
	}

	context.socket_fd_port += args->instance;
	if (context.socket_fd_port > MAX_PORT)
		context.socket_fd_port -= (MAX_PORT - MIN_PORT + 1);
	reserved_port = stress_net_reserve_ports(context.socket_fd_port, context.socket_fd_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, context.socket_fd_port);
		rc = EXIT_NO_RESOURCE;
		goto tidy_file;
	}
	context.socket_fd_port = reserved_port;

	pr_dbg("%s: process [%" PRIdMAX "] using socket port %d and maximum of %zd file descriptors\n",
		args->name, (intmax_t)args->pid, context.socket_fd_port, context.max_fd);

	/*
	 * When run as root, we really don't want to use up all
	 * the file descriptors. Limit ourselves to a head room
	 * so that we don't ever run out of memory
	 */
	if (geteuid() == 0) {
		context.max_fd -= 64;
		context.max_fd /= args->instances ? args->instances : 1;
		if (context.max_fd < 0)
			context.max_fd = 1;
	}
	if (context.max_fd > (1024 * 1024))
		context.max_fd  = 1024 * 1024;

	context.fds_size = sizeof(*context.fds) * (size_t)context.max_fd;
	context.fds = (int *)malloc(context.fds_size);
	if (!context.fds) {
		pr_inf_skip("%s: cannot allocate %zd file descriptors%s, skipping stressor\n",
			args->name, context.max_fd, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto tidy_file;
	}
	if (pthread_barrier_init(&context.barrier, NULL, MAX_PTHREADS) != 0) {
		pr_inf_skip("%s: cannot create pthread barrier, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_fds;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args))) {
			rc = EXIT_SUCCESS;
			goto tidy_barrier;
		}
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy_barrier;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_set_oom_adjustment(args, false);
		rc = stress_race_fd_client(&context);
		_exit(rc);
	} else {
		int status;

		rc = stress_race_fd_server(&context, list);
		(void)shim_kill(pid, SIGALRM);
		(void)shim_waitpid(pid, &status, 0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
tidy_barrier:
	(void)pthread_barrier_destroy(&context.barrier);
tidy_fds:
	free(context.fds);
tidy_file:
	(void)unlink(filename);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);
	stress_fd_race_filename_free(list);

	return rc;
}

const stressor_info_t stress_fd_race_info = {
	.stressor = stress_fd_race,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_fd_race_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "only supported on Linux with pthread support and pthread_barrier"
};
#endif
