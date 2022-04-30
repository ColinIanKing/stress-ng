/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(__NR_userfaultfd)
#define HAVE_USERFAULTFD
#endif

#if defined(HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif

#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif

#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#endif

#if defined(HAVE_SYS_MSG_H)
#include <sys/msg.h>
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#endif

#if defined(HAVE_SYS_TIMERFD_H)
#include <sys/timerfd.h>
#endif

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#endif

#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#endif

#define RESOURCE_FORKS 	(1024)
#define MAX_LOOPS	(1024)

static const stress_help_t help[] = {
	{ NULL,	"resources N",	   "start N workers consuming system resources" },
	{ NULL,	"resources-ops N", "stop after N resource bogo operations" },
	{ NULL,	NULL,		   NULL }
};

typedef struct {
	void *m_malloc;
	void *m_sbrk;
	void *m_mmap;
	size_t m_mmap_size;
	int fd_pipe[2];
	int pipe_ret;
	int fd_open;
	int fd_sock;
	int fd_socketpair[2];
	pid_t pid;
#if defined(HAVE_EVENTFD)
	int fd_ev;
#endif
#if defined(HAVE_MEMFD_CREATE)
	int fd_memfd;
	void *ptr_memfd;
#endif
#if defined(__NR_memfd_secret)
	int fd_memfd_secret;
	void *ptr_memfd_secret;
#endif
#if defined(HAVE_USERFAULTFD)
	int fd_uf;
#endif
#if defined(O_TMPFILE)
	int fd_tmp;
#endif
#if defined(HAVE_LIB_PTHREAD)
	pthread_t pthread;
	int pthread_ret;
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
	pthread_mutex_t mutex;
	int mutex_ret;
#endif
#endif
#if defined(HAVE_SYS_INOTIFY_H)
	int fd_inotify;
	int wd_inotify;
#endif
#if defined(HAVE_PTSNAME)
	int pty_mtx;
	int pty;
#endif
#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
	bool timerok;
	timer_t timerid;
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
	int timer_fd;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
	bool semok;
	sem_t sem;
#endif
#if defined(HAVE_SEM_SYSV)
	int sem_id;
#endif
#if defined(HAVE_MQ_SYSV) &&		\
    defined(HAVE_SYS_IPC_H) &&		\
    defined(HAVE_SYS_MSG_H)
	int msgq_id;
#endif
#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_MQ_POSIX) &&		\
    defined(HAVE_MQUEUE_H)
	mqd_t mq;
	char mq_name[64];
#endif
#if defined(HAVE_PKEY_ALLOC) &&		\
    defined(HAVE_PKEY_FREE)
	int pkey;
#endif
#if defined(HAVE_PIDFD_OPEN)
	int pid_fd;
#endif
} stress_info_t;

static pid_t pids[RESOURCE_FORKS];

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_pthread_func()
 *	pthread that exits immediately
 */
static void *stress_pthread_func(void *ctxt)
{
	static void *nowt;

	(void)ctxt;
	(void)sleep(1);
	return &nowt;
}
#endif

static void NORETURN waste_resources(
	const stress_args_t *args,
	const size_t page_size,
	const size_t pipe_size,
	const size_t mem_slack)
{
#if defined(RLIMIT_MEMLOCK)
	struct rlimit rlim;
#endif
	size_t mlock_size;
	size_t i, n = 0;
	size_t shmall, freemem, totalmem, freeswap;
	const pid_t pid = getpid();
	static const int domains[] = { AF_INET, AF_INET6 };
	static const int types[] = { SOCK_STREAM, SOCK_DGRAM };
	static stress_info_t info[MAX_LOOPS];
#if defined(O_NOATIME)
	const int flag = O_NOATIME;
#else
	const int flag = 0;
#endif

#if defined(RLIMIT_MEMLOCK)
	{
		int ret;

		ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
		if (ret < 0) {
			mlock_size = page_size * MAX_LOOPS;
		} else {
			mlock_size = rlim.rlim_cur;
		}
	}
#else
	UNEXPECTED
	mlock_size = page_size * MAX_LOOPS;
#endif

#if !(defined(HAVE_LIB_RT) &&	\
      defined(HAVE_MQ_POSIX) && \
      defined(HAVE_MQUEUE_H))
	(void)args;
#endif
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);

	if ((shmall + freemem + totalmem > 0) &&
	    (freemem > 0) && (freemem < mem_slack))
		_exit(0);

	(void)memset(&info, 0, sizeof(info));

	for (i = 0; i < MAX_LOOPS; i++) {
		int j;
		size_t posn;
		char tmpfilename[64];
#if defined(HAVE_MEMFD_CREATE)
		char name[32];
#endif
		info[i].m_malloc = NULL;
		info[i].m_mmap = MAP_FAILED;
		info[i].pipe_ret = -1;
		info[i].fd_open = -1;
#if defined(HAVE_EVENTFD)
		info[i].fd_ev = -1;
#endif
#if defined(HAVE_MEMFD_CREATE)
		info[i].fd_memfd = -1;
		info[i].ptr_memfd = NULL;
#endif
#if defined(__NR_memfd_secret)
		info[i].fd_memfd_secret = -1;
		info[i].ptr_memfd_secret = NULL;
#endif
		info[i].fd_sock = -1;
		info[i].fd_socketpair[0] = -1;
		info[i].fd_socketpair[1] = -1;
#if defined(HAVE_USERFAULTFD)
		info[i].fd_uf = -1;
#endif
#if defined(O_TMPFILE)
		info[i].fd_tmp = -1;
#endif
#if defined(HAVE_LIB_PTHREAD)
		info[i].pthread_ret = -1;
		info[i].pthread = 0;
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		(void)memset(&info[i].mutex, 0, sizeof(info[i].mutex));
		info[i].mutex_ret = -1;
#endif
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
		info[i].timerok = false;
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
		info[i].timer_fd = -1;
#endif
#if defined(HAVE_SYS_INOTIFY)
		info[i].wd_inotify = -1;
		info[i].fd_inotify = -1;
#endif
#if defined(HAVE_PTSNAME)
		info[i].pty = -1;
		info[i].pty_mtx = -1;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		info[i].semok = false;
#endif
#if defined(HAVE_SEM_SYSV)
		info[i].sem_id = -1;
#endif
#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		info[i].msgq_id = -1;
#endif
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		info[i].mq = -1;
		info[i].mq_name[0] = '\0';
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		info[i].pkey = -1;
#endif
#if defined(HAVE_PIDFD_OPEN)
		info[i].pid_fd = -1;
#endif
		info[i].pid = 1;

		/*
		 *  Ensure we tidy half complete resources since n is off by one
		 *  if we break out of the loop to early
		 */
		n = i + 1;

		stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);

		(void)snprintf(tmpfilename, sizeof(tmpfilename),
			"/tmp/stress-ng-%8.8x%8.8x-", (int)pid, (int)stress_mwc32());
		posn = strlen(tmpfilename);
		for (j = 0; keep_stressing_flag() && (j < 256); j++) {
			int fd;

			stress_strnrnd(tmpfilename + posn, sizeof(tmpfilename) - posn - 1);
			tmpfilename[sizeof(tmpfilename) - 1] = '\0';
			fd = open(tmpfilename, O_RDONLY);
			if (fd >= 0)
				(void)close(fd);
		}
		if (!keep_stressing_flag())
			break;

		if ((shmall + freemem + totalmem > 0) &&
		    (freemem > 0) && (freemem < mem_slack))
			break;

		if ((stress_mwc8() & 0xf) == 0) {
			info[i].m_malloc = calloc(1, page_size);
			if (!keep_stressing_flag())
				break;
		}
		if ((stress_mwc8() & 0xf) == 0) {
			info[i].m_sbrk = shim_sbrk((intptr_t)page_size);
			if (!keep_stressing_flag())
				break;
		}
		if ((stress_mwc8() & 0xf) == 0) {
			info[i].m_mmap_size = page_size;
			info[i].m_mmap = mmap(NULL, info[i].m_mmap_size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (!keep_stressing_flag())
				break;
			if (info[i].m_mmap != MAP_FAILED) {
				size_t locked = STRESS_MINIMUM(mlock_size, info[i].m_mmap_size);

				(void)stress_madvise_random(info[i].m_mmap, info[i].m_mmap_size);
				(void)stress_mincore_touch_pages_interruptible(info[i].m_mmap, info[i].m_mmap_size);
				if (locked > 0) {
					shim_mlock(info[i].m_mmap, locked);
					mlock_size -= locked;
				}
			}
		}

		info[i].pipe_ret = pipe(info[i].fd_pipe);
#if defined(F_SETPIPE_SZ)
		if (info[i].pipe_ret == 0) {
			(void)fcntl(info[i].fd_pipe[0], F_SETPIPE_SZ, pipe_size);
			(void)fcntl(info[i].fd_pipe[1], F_SETPIPE_SZ, pipe_size);
		}
#else
		UNEXPECTED
		(void)pipe_size;
#endif
		if (!keep_stressing_flag())
			break;
		info[i].fd_open = open("/dev/null", O_RDONLY | flag);
		if (!keep_stressing_flag())
			break;
#if defined(HAVE_EVENTFD)
		info[i].fd_ev = eventfd(0, 0);
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(HAVE_MEMFD_CREATE)
		(void)snprintf(name, sizeof(name), "memfd-%" PRIdMAX "-%zu",
			(intmax_t)pid, i);
		info[i].fd_memfd = shim_memfd_create(name, 0);
		if (info[i].fd_memfd != -1) {
			if (ftruncate(info[i].fd_memfd, (off_t)page_size) == 0) {
				info[i].ptr_memfd = mmap(NULL, page_size,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					info[i].fd_memfd, 0);
				if (info[i].ptr_memfd == MAP_FAILED)
					info[i].ptr_memfd = NULL;
			}
		}
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(__NR_memfd_secret)
		info[i].fd_memfd_secret = shim_memfd_secret(0);
		if (info[i].fd_memfd_secret != -1) {
			if (ftruncate(info[i].fd_memfd_secret, page_size) == 0) {
				info[i].ptr_memfd_secret = mmap(NULL,
					page_size,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					info[i].fd_memfd_secret, 0);
				if (info[i].ptr_memfd_secret == MAP_FAILED)
					info[i].ptr_memfd_secret = NULL;
			}
		}
		if (!keep_stressing_flag())
			break;
#endif
		info[i].fd_sock = socket(
			domains[stress_mwc32() % SIZEOF_ARRAY(domains)],
			types[stress_mwc32() % SIZEOF_ARRAY(types)], 0);
		if (!keep_stressing_flag())
			break;

		if (socketpair(AF_UNIX, SOCK_STREAM, 0,
			info[i].fd_socketpair) < 0) {
			info[i].fd_socketpair[0] = -1;
			info[i].fd_socketpair[1] = -1;
		}

#if defined(HAVE_USERFAULTFD)
		info[i].fd_uf = shim_userfaultfd(0);
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(O_TMPFILE)
		info[i].fd_tmp = open("/tmp", O_TMPFILE | O_RDWR | flag,
				      S_IRUSR | S_IWUSR);
		if (!keep_stressing_flag())
			break;
		if (info[i].fd_tmp != -1) {
			size_t sz = page_size * stress_mwc32();

			(void)shim_fallocate(info[i].fd_tmp, 0, 0, (off_t)sz);
#if defined(F_GETLK) &&		\
    defined(F_SETLK) &&		\
    defined(F_SETLKW) &&	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
			{
				struct flock f;

				f.l_type = F_WRLCK;
				f.l_whence = SEEK_SET;
				f.l_start = 0;
				f.l_len = (off_t)sz;
				f.l_pid = pid;

				(void)fcntl(info[i].fd_tmp, F_SETLK, &f);
			}
		}
#else
		UNEXPECTED
#endif
#else
		UNEXPECTED
#endif
#if defined(HAVE_SYS_INOTIFY_H)
		info[i].fd_inotify = inotify_init();
		if (info[i].fd_inotify > -1) {
			info[i].wd_inotify = inotify_add_watch(
				info[i].fd_inotify, ".",
				0
#if defined(IN_ACCESS)
				| IN_ACCESS
#endif
#if defined(IN_MODIFY)
				| IN_MODIFY
#endif
#if defined(IN_ATTRIB)
				| IN_ATTRIB
#endif
#if defined(IN_CLOSE_WRITE)
				| IN_CLOSE_WRITE
#endif
#if defined(IN_OPEN)
				| IN_OPEN
#endif
#if defined(IN_MOVED_FROM)
				| IN_MOVED_FROM
#endif
#if defined(IN_MOVED_TO)
				| IN_MOVED_TO
#endif
#if defined(IN_CREATE)
				| IN_CREATE
#endif
#if defined(IN_DELETE)
				| IN_DELETE
#endif
#if defined(IN_DELETE_SELF)
				| IN_DELETE_SELF
#endif
#if defined(IN_MOVE_SELF)
				| IN_MOVE_SELF
#endif
				);
		} else {
			info[i].fd_inotify = -1;
			info[i].wd_inotify = -1;
		}
		if (!keep_stressing_flag())
			break;
#endif
#if defined(HAVE_PTSNAME)
		info[i].pty_mtx = open("/dev/ptmx", O_RDWR | flag);
		info[i].pty = -1;
		if (info[i].pty_mtx >= 0) {
			const char *ptyname = ptsname(info[i].pty_mtx);

			if (ptyname)
				info[i].pty = open(ptyname, O_RDWR | flag);
		}
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_LIB_PTHREAD)
		if (!i) {
			info[i].pthread_ret =
				pthread_create(&info[i].pthread, NULL,
					stress_pthread_func, NULL);
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
			info[i].mutex_ret = pthread_mutex_init(&info[i].mutex, NULL);
#endif
			if (!keep_stressing_flag())
				break;
		}
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
		if (!i) {
			struct sigevent sevp;

			sevp.sigev_notify = SIGEV_NONE;
			sevp.sigev_signo = SIGUNUSED;
			sevp.sigev_value.sival_ptr = &info[i].timerid;
			info[i].timerok =
				(timer_create(CLOCK_REALTIME, &sevp, &info[i].timerid) == 0);
			if (!keep_stressing_flag())
				break;
		}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
		if (!i) {
			info[i].timer_fd = timerfd_create(CLOCK_REALTIME, 0);
			if (!keep_stressing_flag())
				break;
		}
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		info[i].semok = (sem_init(&info[i].sem, 1, 1) >= 0);
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_SEM_SYSV)
		key_t sem_key = (key_t)stress_mwc32();
		info[i].sem_id = semget(sem_key, 1,
			IPC_CREAT | S_IRUSR | S_IWUSR);
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		info[i].msgq_id = msgget(IPC_PRIVATE,
				S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		struct mq_attr attr;

		(void)snprintf(info[i].mq_name, sizeof(info[i].mq_name),
			"/%s-%i-%" PRIu32 "-%zu",
			args->name, getpid(), args->instance, i);
		attr.mq_flags = 0;
		attr.mq_maxmsg = 1;
		attr.mq_msgsize = 32;
		attr.mq_curmsgs = 0;

		info[i].mq = mq_open(info[i].mq_name,
			O_CREAT | O_RDWR | flag, S_IRUSR | S_IWUSR, &attr);
		if (!keep_stressing_flag())
			break;
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		info[i].pkey = shim_pkey_alloc(0, 0);
#endif
		if (!keep_stressing_flag())
			break;

#if defined(HAVE_PIDFD_OPEN)
		info[i].pid_fd = shim_pidfd_open(getpid(), 0);
#endif

		info[i].pid = fork();
		if (info[i].pid == 0) {
			(void)sleep(10);
			_exit(0);
		}
	}

	/*  Sanity check, ensure n is never > MAX_LOOPS */
	n = STRESS_MINIMUM(MAX_LOOPS, n);

	for (i = 0; i < n; i++) {
		if (info[i].m_malloc)
			free(info[i].m_malloc);
		if (info[i].m_mmap && (info[i].m_mmap != MAP_FAILED)) {
			(void)shim_munlock(info[i].m_mmap, info[i].m_mmap_size);
			(void)munmap(info[i].m_mmap, info[i].m_mmap_size);
		}
		if (info[i].pipe_ret != -1) {
			(void)close(info[i].fd_pipe[0]);
			(void)close(info[i].fd_pipe[1]);
		}
		if (info[i].fd_open != -1)
			(void)close(info[i].fd_open);
#if defined(HAVE_EVENTFD)
		if (info[i].fd_ev != -1)
			(void)close(info[i].fd_ev);
#endif
#if defined(HAVE_MEMFD_CREATE)
		if (info[i].fd_memfd != -1)
			(void)close(info[i].fd_memfd);
		if (info[i].ptr_memfd)
			(void)munmap(info[i].ptr_memfd, page_size);
#endif
#if defined(__NR_memfd_secret)
		if (info[i].fd_memfd_secret != -1)
			(void)close(info[i].fd_memfd_secret);
		if (info[i].ptr_memfd_secret)
			(void)munmap(info[i].ptr_memfd_secret, page_size);
#endif
		if (info[i].fd_sock != -1)
			(void)close(info[i].fd_sock);
		if (info[i].fd_socketpair[0] != -1)
			(void)close(info[i].fd_socketpair[0]);
		if (info[i].fd_socketpair[1] != -1)
			(void)close(info[i].fd_socketpair[1]);

#if defined(HAVE_USERFAULTFD)
		if (info[i].fd_uf != -1)
			(void)close(info[i].fd_uf);
#endif

#if defined(O_TMPFILE)
		if (info[i].fd_tmp != -1)
			(void)close(info[i].fd_tmp);
#endif

#if defined(HAVE_LIB_PTHREAD)
		if ((!i) && (!info[i].pthread_ret) && (info[i].pthread))
			(void)pthread_join(info[i].pthread, NULL);
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
			(void)pthread_mutex_destroy(&info[i].mutex);
#endif
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
		if ((!i) && (info[i].timerok))
			(void)timer_delete(info[i].timerid);
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
		if ((!i) && (info[i].timer_fd != -1))
			(void)close(info[i].timer_fd);
#endif

#if defined(HAVE_SYS_INOTIFY)
		if (info[i].wd_inotify != -1)
			(void)inotify_rm_watch(info[i].fd_inotify, info[i].wd_inotify);
		if (info[i].fd_inotify != -1)
			(void)close(info[i].fd_inotify);
#endif

#if defined(HAVE_PTSNAME)
		if (info[i].pty != -1)
			(void)close(info[i].pty);
		if (info[i].pty_mtx != -1)
			(void)close(info[i].pty_mtx);
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		if (info[i].semok)
			(void)sem_destroy(&info[i].sem);
#endif

#if defined(HAVE_SEM_SYSV)
		if (info[i].sem_id >= 0)
			(void)semctl(info[i].sem_id, 0, IPC_RMID);
#endif

#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		if (info[i].msgq_id >= 0)
			(void)msgctl(info[i].msgq_id, IPC_RMID, NULL);
#endif

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		if (info[i].mq >= 0)
			(void)mq_close(info[i].mq);
		if (info[i].mq_name[0])
			(void)mq_unlink(info[i].mq_name);
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		if (info[i].pkey > -1)
			 (void)shim_pkey_free(info[i].pkey);
#endif

#if defined(HAVE_PIDFD_OPEN)
		if (info[i].pid_fd > -1)
			(void)close(info[i].pid_fd);
#endif
		if (info[i].pid > 0) {
			int status;

			(void)stress_killpid(info[i].pid);
			(void)shim_waitpid(info[i].pid, &status, 0);
		}
	}
	_exit(0);
}

static void MLOCKED_TEXT kill_children(const size_t resource_forks)
{
	size_t i;

	for (i = 0; i < resource_forks; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGALRM);
	}

	for (i = 0; i < resource_forks; i++) {
		if (pids[i] > 0) {
			int status;

			(void)shim_waitpid(pids[i], &status, 0);
		}
	}
}

/*
 *  stress_resources()
 *	stress by forking and exiting
 */
static int stress_resources(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t pipe_size = stress_probe_max_pipe_size();
	size_t mem_slack;
	size_t shmall, freemem, totalmem, freeswap, resource_forks = 0;

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);
	if (totalmem > 0) {
		resource_forks = totalmem / (args->num_instances * MAX_LOOPS * 16 * KB);
	}
	if (resource_forks < 1)
		resource_forks = 1;
	if (resource_forks > RESOURCE_FORKS)
		resource_forks = RESOURCE_FORKS;
	mem_slack = (args->num_instances * resource_forks * MB);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int i;

		(void)memset(pids, 0, sizeof(pids));
		for (i = 0; i < resource_forks; i++) {
			pid_t pid;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);
			if (totalmem > 0 && totalmem < mem_slack)
				break;

			pid = fork();
			if (pid == 0) {
				int ret;

				(void)setpgid(0, g_pgrp);
				stress_set_oom_adjustment(args->name, true);
				ret = stress_drop_capabilities(args->name);
				(void)ret;
				(void)sched_settings_apply(true);

				waste_resources(args, page_size, pipe_size, mem_slack);
				_exit(0); /* should never get here */
			}

			if (pid > -1)
				(void)setpgid(pids[i], g_pgrp);
			pids[i] = pid;

			if (!keep_stressing(args)) {
				kill_children(resource_forks);
				return EXIT_SUCCESS;
			}
			inc_counter(args);
		}
		kill_children(resource_forks);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_resources_info = {
	.stressor = stress_resources,
	.class = CLASS_MEMORY | CLASS_OS,
	.help = help
};
