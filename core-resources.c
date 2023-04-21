/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-resources.h"

#if defined(HAVE_LIB_PTHREAD)
static void *stress_resources_pthread_func(void *ctxt)
{
        static void *nowt;

        (void)ctxt;
        (void)sleep(1);
        return &nowt;
}
#endif

/*
 *  stress_resources_allocate()
 *	allocate a wide range of resources, perform num_resources
 *	resource allocations
 */
size_t stress_resources_allocate(
	const stress_args_t *args,
	stress_resources_t *resources,
        const size_t num_resources,
	const size_t pipe_size,
	const size_t min_mem_free,
	const bool do_fork)
{
#if defined(RLIMIT_MEMLOCK)
	struct rlimit rlim;
#endif
	size_t mlock_size;
	size_t i, n = 0;
	size_t shmall, freemem, totalmem, freeswap, totalswap;
	const pid_t pid = getpid();
	const size_t page_size = args->page_size;
	static const int domains[] = { AF_INET, AF_INET6 };
	static const int types[] = { SOCK_STREAM, SOCK_DGRAM };

	stress_ksm_memory_merge(1);

	(void)pid;
	(void)page_size;

#if defined(RLIMIT_MEMLOCK)
	{
		int ret;

		ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
		if (ret < 0) {
			mlock_size = page_size * num_resources;
		} else {
			mlock_size = rlim.rlim_cur;
		}
	}
#else
	UNEXPECTED
	mlock_size = page_size * num_resources;
#endif

#if !(defined(HAVE_LIB_RT) &&	\
      defined(HAVE_MQ_POSIX) && \
      defined(HAVE_MQUEUE_H))
	(void)args;
#endif
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	if ((freemem > 0) && (freemem < min_mem_free))
		return 0;

	for (i = 0; i < num_resources; i++) {
#if defined(HAVE_MEMFD_CREATE)
		char name[64];
#endif
		resources[i].m_malloc = NULL;
		resources[i].m_mmap = MAP_FAILED;
		resources[i].pipe_ret = -1;
		resources[i].fd_open = -1;
#if defined(HAVE_EVENTFD)
		resources[i].fd_ev = -1;
#endif
#if defined(HAVE_MEMFD_CREATE)
		resources[i].fd_memfd = -1;
		resources[i].ptr_memfd = NULL;
#endif
#if defined(__NR_memfd_secret)
		resources[i].fd_memfd_secret = -1;
		resources[i].ptr_memfd_secret = NULL;
#endif
		resources[i].fd_sock = -1;
		resources[i].fd_socketpair[0] = -1;
		resources[i].fd_socketpair[1] = -1;
#if defined(HAVE_USERFAULTFD)
		resources[i].fd_uf = -1;
#endif
#if defined(O_TMPFILE)
		resources[i].fd_tmp = -1;
#endif
#if defined(HAVE_LIB_PTHREAD)
		resources[i].pthread_ret = -1;
		resources[i].pthread = (pthread_t)0;
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		(void)memset(&resources[i].mutex, 0, sizeof(resources[i].mutex));
		resources[i].mutex_ret = -1;
#endif
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
		resources[i].timerok = false;
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
		resources[i].timer_fd = -1;
#endif
#if defined(HAVE_SYS_INOTIFY)
		resources[i].wd_inotify = -1;
		resources[i].fd_inotify = -1;
#endif
#if defined(HAVE_PTSNAME)
		resources[i].pty = -1;
		resources[i].pty_mtx = -1;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		resources[i].semok = false;
#endif
#if defined(HAVE_SEM_SYSV)
		resources[i].sem_id = -1;
#endif
#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		resources[i].msgq_id = -1;
#endif
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		resources[i].mq = -1;
		resources[i].mq_name[0] = '\0';
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		resources[i].pkey = -1;
#endif
#if defined(HAVE_PIDFD_OPEN)
		resources[i].pid_fd = -1;
#endif
		resources[i].pid = 0;

		/*
		 *  Ensure we tidy half complete resources since n is off by one
		 *  if we break out of the loop to early
		 */
		n = i + 1;

		if (!keep_stressing_flag())
			break;

		stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
		if ((freemem > 0) && (freemem < min_mem_free))
			break;

		if ((stress_mwc8() & 0xf) == 0) {
			resources[i].m_malloc = calloc(1, page_size);
			if (!keep_stressing_flag())
				break;
		}
		if ((stress_mwc8() & 0xf) == 0) {
			resources[i].m_sbrk = shim_sbrk((intptr_t)page_size);
			if (!keep_stressing_flag())
				break;
		}
		if ((stress_mwc8() & 0xf) == 0) {
			resources[i].m_mmap_size = page_size * 2;
			resources[i].m_mmap = mmap(NULL, resources[i].m_mmap_size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (!keep_stressing_flag())
				break;
			if (resources[i].m_mmap != MAP_FAILED) {
				const size_t locked = STRESS_MINIMUM(mlock_size, resources[i].m_mmap_size);

				(void)stress_madvise_random(resources[i].m_mmap, resources[i].m_mmap_size);
				(void)stress_mincore_touch_pages_interruptible(resources[i].m_mmap, resources[i].m_mmap_size);
				if (locked > 0) {
					shim_mlock(resources[i].m_mmap, locked);
					mlock_size -= locked;
				}
			}
		}
		if (resources[i].m_mmap)

		resources[i].pipe_ret = pipe(resources[i].fd_pipe);
#if defined(F_SETPIPE_SZ)
		if (resources[i].pipe_ret == 0) {
			(void)fcntl(resources[i].fd_pipe[0], F_SETPIPE_SZ, pipe_size);
			(void)fcntl(resources[i].fd_pipe[1], F_SETPIPE_SZ, pipe_size);
		}
#else
		UNEXPECTED
		(void)pipe_size;
#endif
		if (!keep_stressing_flag())
			break;
		resources[i].fd_open = open("/dev/null", O_RDONLY);
		if (!keep_stressing_flag())
			break;
#if defined(HAVE_EVENTFD)
		resources[i].fd_ev = eventfd(0, 0);
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(HAVE_MEMFD_CREATE)
		(void)snprintf(name, sizeof(name), "memfd-%" PRIdMAX "-%zu",
			(intmax_t)pid, i);
		resources[i].fd_memfd = shim_memfd_create(name, 0);
		if (resources[i].fd_memfd != -1) {
			if (ftruncate(resources[i].fd_memfd, (off_t)page_size) == 0) {
				resources[i].ptr_memfd = mmap(NULL, page_size,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					resources[i].fd_memfd, 0);
				if (resources[i].ptr_memfd == MAP_FAILED)
					resources[i].ptr_memfd = NULL;
			}
			shim_fallocate(resources[i].fd_memfd, 0, 0, (off_t)stress_mwc16());
		}
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(__NR_memfd_secret)
		resources[i].fd_memfd_secret = shim_memfd_secret(0);
		if (resources[i].fd_memfd_secret != -1) {
			if (ftruncate(resources[i].fd_memfd_secret, (off_t)page_size) == 0) {
				resources[i].ptr_memfd_secret = mmap(NULL,
					page_size,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					resources[i].fd_memfd_secret, 0);
				if (resources[i].ptr_memfd_secret == MAP_FAILED)
					resources[i].ptr_memfd_secret = NULL;
			}
		}
		if (!keep_stressing_flag())
			break;
#endif
		resources[i].fd_sock = socket(
			domains[stress_mwc32modn((uint32_t)SIZEOF_ARRAY(domains))],
			types[stress_mwc32modn((uint32_t)SIZEOF_ARRAY(types))], 0);
		if (!keep_stressing_flag())
			break;

		if (socketpair(AF_UNIX, SOCK_STREAM, 0,
			resources[i].fd_socketpair) < 0) {
			resources[i].fd_socketpair[0] = -1;
			resources[i].fd_socketpair[1] = -1;
		}

#if defined(HAVE_USERFAULTFD)
		resources[i].fd_uf = shim_userfaultfd(0);
		if (!keep_stressing_flag())
			break;
#else
		UNEXPECTED
#endif
#if defined(O_TMPFILE)
		resources[i].fd_tmp = open("/tmp", O_TMPFILE | O_RDWR,
				      S_IRUSR | S_IWUSR);
		if (!keep_stressing_flag())
			break;
		if (resources[i].fd_tmp != -1) {
			const size_t sz = page_size * stress_mwc32();

			(void)shim_fallocate(resources[i].fd_tmp, 0, 0, (off_t)sz);
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

				(void)fcntl(resources[i].fd_tmp, F_SETLK, &f);
			}
#else
		UNEXPECTED
#endif
		}
#else
		UNEXPECTED
#endif
#if defined(HAVE_SYS_INOTIFY_H)
		resources[i].fd_inotify = inotify_init();
		if (resources[i].fd_inotify > -1) {
			resources[i].wd_inotify = inotify_add_watch(
				resources[i].fd_inotify, ".",
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
			resources[i].fd_inotify = -1;
			resources[i].wd_inotify = -1;
		}
		if (!keep_stressing_flag())
			break;
#endif
#if defined(HAVE_PTSNAME)
		resources[i].pty_mtx = open("/dev/ptmx", O_RDWR);
		resources[i].pty = -1;
		if (resources[i].pty_mtx >= 0) {
			const char *ptyname = ptsname(resources[i].pty_mtx);

			if (ptyname)
				resources[i].pty = open(ptyname, O_RDWR);
		}
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_LIB_PTHREAD)
		if (!i) {
			resources[i].pthread_ret =
				pthread_create(&resources[i].pthread, NULL,
					stress_resources_pthread_func, NULL);
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
			resources[i].mutex_ret = pthread_mutex_init(&resources[i].mutex, NULL);
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
			sevp.sigev_value.sival_ptr = &resources[i].timerid;
			resources[i].timerok =
				(timer_create(CLOCK_REALTIME, &sevp, &resources[i].timerid) == 0);
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
			resources[i].timer_fd = timerfd_create(CLOCK_REALTIME, 0);
			if (!keep_stressing_flag())
				break;
		}
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		resources[i].semok = (sem_init(&resources[i].sem, 1, 1) >= 0);
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_KEY_T)
		{
			/* Use even key so it won't clash with odd global sem key */
			const key_t sem_key = (key_t)stress_mwc32() & ~(key_t)1;

			resources[i].sem_id = semget(sem_key, 1,
				IPC_CREAT | S_IRUSR | S_IWUSR);
			if (!keep_stressing_flag())
				break;
		}
#endif

#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		resources[i].msgq_id = msgget(IPC_PRIVATE,
				S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
		if (!keep_stressing_flag())
			break;
#endif

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		{
			struct mq_attr attr;

			(void)snprintf(resources[i].mq_name, sizeof(resources[i].mq_name),
				"/%s-%" PRIdMAX "-%" PRIu32 "-%zu",
				args->name, (intmax_t)pid, args->instance, i);
			attr.mq_flags = 0;
			attr.mq_maxmsg = 1;
			attr.mq_msgsize = 32;
			attr.mq_curmsgs = 0;

			resources[i].mq = mq_open(resources[i].mq_name,
				O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
			if (!keep_stressing_flag())
				break;
		}
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		resources[i].pkey = shim_pkey_alloc(0, 0);
#endif
		if (!keep_stressing_flag())
			break;

#if defined(HAVE_PIDFD_OPEN)
		resources[i].pid_fd = shim_pidfd_open(pid, 0);
#endif

		if (do_fork) {
			resources[i].pid = fork();
			if (resources[i].pid == 0) {
				(void)sleep(10);
				_exit(0);
			}
		} else {
			resources[i].pid = 0;
		}
	}

	/*
	 *  Create mmap holes
	 */
	for (i = 0; i < num_resources; i++) {
		if (resources[i].m_mmap && (resources[i].m_mmap != MAP_FAILED)) {
			if (resources[i].m_mmap_size > page_size) {
				const size_t free_size = resources[i].m_mmap_size - page_size;
				const uintptr_t addr = (uintptr_t)resources[i].m_mmap + page_size;

				(void)munmap((void *)addr, free_size);
				resources[i].m_mmap_size -= page_size;
			}
		}
	}

	return STRESS_MINIMUM(num_resources, n);
}

/*
 *  stress_resources_free()
 *	free a wide range of resources, perform num_resources
 *	resource frees
 */
void stress_resources_free(
	const stress_args_t *args,
	stress_resources_t *resources,
        const size_t num_resources)
{
	const size_t page_size = args->page_size;
	size_t i;

	(void)page_size;

	for (i = 0; i < num_resources; i++) {
		if (resources[i].m_malloc) {
			free(resources[i].m_malloc);
			resources[i].m_malloc = NULL;
		}
		if (resources[i].m_mmap && (resources[i].m_mmap != MAP_FAILED)) {
			(void)shim_munlock(resources[i].m_mmap, resources[i].m_mmap_size);
			(void)munmap(resources[i].m_mmap, resources[i].m_mmap_size);
			resources[i].m_mmap = MAP_FAILED;
		}
		if (resources[i].pipe_ret != -1) {
			(void)close(resources[i].fd_pipe[0]);
			(void)close(resources[i].fd_pipe[1]);
			resources[i].fd_pipe[0] = -1;
			resources[i].fd_pipe[1] = -1;
		}
		if (resources[i].fd_open != -1) {
			(void)close(resources[i].fd_open);
			resources[i].fd_open = -1;
		}
#if defined(HAVE_EVENTFD)
		if (resources[i].fd_ev != -1) {
			(void)close(resources[i].fd_ev);
			resources[i].fd_ev = -1;
		}
#endif
#if defined(HAVE_MEMFD_CREATE)
		if (resources[i].fd_memfd != -1) {
			(void)close(resources[i].fd_memfd);
			resources[i].fd_memfd = -1;
		}
		if (resources[i].ptr_memfd) {
			(void)munmap(resources[i].ptr_memfd, page_size);
			resources[i].ptr_memfd = MAP_FAILED;
		}
#endif
#if defined(__NR_memfd_secret)
		if (resources[i].fd_memfd_secret != -1) {
			(void)close(resources[i].fd_memfd_secret);
			resources[i].fd_memfd_secret = -1;
		}
		if (resources[i].ptr_memfd_secret) {
			(void)munmap(resources[i].ptr_memfd_secret, page_size);
			resources[i].ptr_memfd_secret = NULL;
		}
#endif
		if (resources[i].fd_sock != -1) {
			(void)close(resources[i].fd_sock);
			resources[i].fd_sock = -1;
		}
		if (resources[i].fd_socketpair[0] != -1) {
			(void)close(resources[i].fd_socketpair[0]);
			resources[i].fd_socketpair[0] = -1;
		}
		if (resources[i].fd_socketpair[1] != -1) {
			(void)close(resources[i].fd_socketpair[1]);
			resources[i].fd_socketpair[1] = -1;
		}

#if defined(HAVE_USERFAULTFD)
		if (resources[i].fd_uf != -1) {
			(void)close(resources[i].fd_uf);
			resources[i].fd_uf = -1;
		}
#endif

#if defined(O_TMPFILE)
		if (resources[i].fd_tmp != -1) {
			(void)close(resources[i].fd_tmp);
			resources[i].fd_tmp = -1;
		}
#endif

#if defined(HAVE_LIB_PTHREAD)
		if ((!i) && (!resources[i].pthread_ret) && (resources[i].pthread)) {
			(void)pthread_join(resources[i].pthread, NULL);
			resources[i].pthread = (pthread_t)0;
#if defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
			(void)pthread_mutex_destroy(&resources[i].mutex);
			(void)memset(&resources[i].mutex, 0, sizeof(resources[i].mutex));
#endif
		}
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(SIGUNUSED)
		if ((!i) && (resources[i].timerok)) {
			(void)timer_delete(resources[i].timerid);
			resources[i].timerok = false;
		}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
		if ((!i) && (resources[i].timer_fd != -1)) {
			(void)close(resources[i].timer_fd);
			resources[i].timer_fd = -1;
		}
#endif

#if defined(HAVE_SYS_INOTIFY)
		if (resources[i].wd_inotify != -1) {
			(void)inotify_rm_watch(resources[i].fd_inotify, resources[i].wd_inotify);
			resources[i].wd_inotify = -1;
		}
		if (resources[i].fd_inotify != -1) {
			(void)close(resources[i].fd_inotify);
			resources[i].fd_inotify = -1;
		}
#endif

#if defined(HAVE_PTSNAME)
		if (resources[i].pty != -1) {
			(void)close(resources[i].pty);
			resources[i].pty = -1;
		}
		if (resources[i].pty_mtx != -1) {
			(void)close(resources[i].pty_mtx);
			resources[i].pty_mtx = -1;
		}
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		if (resources[i].semok) {
			(void)sem_destroy(&resources[i].sem);
			resources[i].semok = false;
		}
#endif

#if defined(HAVE_SEM_SYSV)
		if (resources[i].sem_id >= 0) {
			(void)semctl(resources[i].sem_id, 0, IPC_RMID);
			resources[i].sem_id = -1;
		}
#endif

#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		if (resources[i].msgq_id >= 0) {
			(void)msgctl(resources[i].msgq_id, IPC_RMID, NULL);
			resources[i].msgq_id = -1;
		}
#endif

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX) &&	\
    defined(HAVE_MQUEUE_H)
		if (resources[i].mq >= 0) {
			(void)mq_close(resources[i].mq);
			resources[i].mq = -1;
		}
		if (resources[i].mq_name[0]) {
			(void)mq_unlink(resources[i].mq_name);
			resources[i].mq_name[0] = '\0';
		}
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		if (resources[i].pkey > -1) {
			 (void)shim_pkey_free(resources[i].pkey);
			 resources[i].pkey = -1;
		}
#endif

#if defined(HAVE_PIDFD_OPEN)
		if (resources[i].pid_fd > -1) {
			(void)close(resources[i].pid_fd);
			resources[i].pid_fd = -1;
		}
#endif
		if (resources[i].pid > 0) {
			int status;

			(void)stress_killpid(resources[i].pid);
			(void)shim_waitpid(resources[i].pid, &status, 0);
			resources[i].pid = 0;
		}
	}
}

