/*
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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-pthread.h"
#include "core-resources.h"

#if defined(HAVE_LIB_PTHREAD)
static void *stress_resources_pthread_func(void *ctxt)
{
        (void)ctxt;
        (void)sleep(1);

        return &g_nowt;
}
#endif

/*
 *  stress_resources_init()
 *	helper function to set resources to initial 'unallocated' values
 */
static void stress_resources_init(stress_resources_t *resources, const size_t num_resources)
{
	size_t i;

	for (i = 0; i < num_resources; i++) {
		resources[i].m_malloc = NULL;
		resources[i].m_malloc_size = 0;
		resources[i].m_sbrk = NULL;
		resources[i].m_sbrk_size = 0;
		resources[i].m_mmap = MAP_FAILED;
		resources[i].m_mmap_size = 0;
		resources[i].fd_pipe[0] = -1;
		resources[i].fd_pipe[1] = -1;
		resources[i].pipe_ret = -1;
		resources[i].fd_open = -1;
		resources[i].fd_sock = -1;
		resources[i].fd_socketpair[0] = -1;
		resources[i].fd_socketpair[1] = -1;
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
#if defined(HAVE_USERFAULTFD)
		resources[i].fd_uf = -1;
#endif
#if defined(O_TMPFILE)
		resources[i].fd_tmp = -1;
#endif
#if defined(HAVE_LIB_PTHREAD)
		resources[i].pthread_ret = -1;
		resources[i].pthread = (pthread_t)0;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		(void)shim_memset(&resources[i].mutex, 0, sizeof(resources[i].mutex));
		resources[i].mutex_ret = -1;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_THREADS_H) &&		\
    defined(HAVE_MTX_T) &&		\
    defined(HAVE_MTX_DESTROY) &&	\
    defined(HAVE_MTX_INIT)
		(void)shim_memset(&resources[i].mtx, 0, sizeof(resources[i].mtx));
		resources[i].mtx_ret = -1;
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
#if defined(HAVE_PIDFD_GETFD)
		resources[i].pid_fd_getfd = -1;
#endif
#endif
		resources[i].pid = 0;
	}
}

/*
 *  stress_resources_allocate()
 *	allocate a wide range of resources, perform num_resources
 *	resource allocations
 */
size_t stress_resources_allocate(
	stress_args_t *args,
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
#if defined(HAVE_PIDFD_OPEN)
	const pid_t ppid = getppid();
#endif
	const size_t page_size = args->page_size;
	static const int domains[] = {
		AF_INET,
#if defined(AF_INET6)
		AF_INET6,
#endif
	};
	static const int types[] = { SOCK_STREAM, SOCK_DGRAM };

	stress_resources_init(resources, num_resources);
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

	(void)shim_memset(resources, 0, sizeof(*resources) * num_resources);

	for (i = 0; i < num_resources; i++) {
#if defined(HAVE_MEMFD_CREATE)
		char name[64];
#endif
		resources[i].m_malloc = NULL;
		resources[i].m_malloc_size = 0;
		resources[i].m_mmap = MAP_FAILED;
		resources[i].m_mmap_size = 0;
		resources[i].pipe_ret = -1;
		resources[i].fd_open = -1;
		resources[i].fd_sock = -1;
		resources[i].fd_socketpair[0] = -1;
		resources[i].fd_socketpair[1] = -1;
		resources[i].pid = 0;
#if defined(HAVE_EVENTFD)
		resources[i].fd_ev = -1;
#endif
#if defined(HAVE_MEMFD_CREATE)
		resources[i].fd_memfd = -1;
		resources[i].ptr_memfd = NULL;
		resources[i].ptr_memfd_size = 0;
#endif
#if defined(__NR_memfd_secret)
		resources[i].fd_memfd_secret = -1;
		resources[i].ptr_memfd_secret = NULL;
		resources[i].ptr_memfd_secret_size = 0;
#endif
#if defined(HAVE_USERFAULTFD)
		resources[i].fd_uf = -1;
#endif
#if defined(O_TMPFILE)
		resources[i].fd_tmp = -1;
#endif
#if defined(HAVE_LIB_PTHREAD)
		resources[i].pthread_ret = -1;
		resources[i].pthread = (pthread_t)0;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		(void)shim_memset(&resources[i].mutex, 0, sizeof(resources[i].mutex));
		resources[i].mutex_ret = -1;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_THREADS_H) &&		\
    defined(HAVE_MTX_T) &&		\
    defined(HAVE_MTX_DESTROY) &&	\
    defined(HAVE_MTX_INIT)
		(void)shim_memset(&resources[i].mtx, 0, sizeof(resources[i].mtx));
		resources[i].mtx_ret = -1;
#endif
#if defined(HAVE_SYS_INOTIFY)
		resources[i].wd_inotify = -1;
		resources[i].fd_inotify = -1;
#endif
#if defined(HAVE_PTSNAME)
		resources[i].pty = -1;
		resources[i].pty_mtx = -1;
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
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		resources[i].semok = false;
		(void)shim_memset(&resources[i].sem, 0, sizeof(resources[i].sem));
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
#if defined(HAVE_PIDFD_GETFD)
		resources[i].pid_fd_getfd = -1;
#endif
#endif
		/*
		 *  Ensure we tidy half complete resources since n is off by one
		 *  if we break out of the loop to early
		 */
		n = i + 1;

		if (UNLIKELY(!stress_continue_flag()))
			break;

		stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
		if (UNLIKELY((freemem > 0) && (freemem < min_mem_free)))
			break;

		if (UNLIKELY((stress_mwc8() & 0xf) == 0)) {
			resources[i].m_malloc = (void *)calloc(1, page_size);
			resources[i].m_malloc_size = page_size;
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
		if (UNLIKELY((stress_mwc8() & 0xf) == 0)) {
			resources[i].m_sbrk = shim_sbrk((intptr_t)page_size);
			resources[i].m_sbrk_size = page_size;
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
		if (UNLIKELY((stress_mwc8() & 0xf) == 0)) {
			resources[i].m_mmap_size = page_size * 2;
			resources[i].m_mmap = mmap(NULL, resources[i].m_mmap_size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (UNLIKELY(!stress_continue_flag()))
				break;
			if (resources[i].m_mmap != MAP_FAILED) {
				const size_t locked = STRESS_MINIMUM(mlock_size, resources[i].m_mmap_size);

				stress_set_vma_anon_name(resources[i].m_mmap, resources[i].m_mmap_size, "resources-mmap");
				(void)stress_madvise_randomize(resources[i].m_mmap, resources[i].m_mmap_size);
				(void)stress_mincore_touch_pages_interruptible(resources[i].m_mmap, resources[i].m_mmap_size);
				if (locked > 0) {
					shim_mlock(resources[i].m_mmap, locked);
					mlock_size -= locked;
				}
				(void)stress_madvise_mergeable(resources[i].m_mmap, resources[i].m_mmap_size);
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
		if (UNLIKELY(!stress_continue_flag()))
			break;
		resources[i].fd_open = open("/dev/null", O_RDONLY);
		if (UNLIKELY(!stress_continue_flag()))
			break;
#if defined(HAVE_EVENTFD)
		resources[i].fd_ev = eventfd(0, 0);
		if (UNLIKELY(!stress_continue_flag()))
			break;
#else
		UNEXPECTED
#endif
#if defined(HAVE_MEMFD_CREATE)

#if !defined(MFD_NOEXEC_SEAL)
#define MFD_NOEXEC_SEAL		0x0008U
#endif
		(void)snprintf(name, sizeof(name), "memfd-%" PRIdMAX "-%zu",
			(intmax_t)pid, i);
		/* Try with MFD_NOEXEC_SEAL */
		resources[i].fd_memfd = shim_memfd_create(name, MFD_NOEXEC_SEAL);
		/* ..and if failed, retry with no flags */
		if (resources[i].fd_memfd == -1)
			resources[i].fd_memfd = shim_memfd_create(name, 0);
		if (resources[i].fd_memfd != -1) {
			if (ftruncate(resources[i].fd_memfd, (off_t)page_size) == 0) {
				resources[i].ptr_memfd = mmap(NULL, page_size,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					resources[i].fd_memfd, 0);
				if (resources[i].ptr_memfd == MAP_FAILED)
					resources[i].ptr_memfd = NULL;
				else {
					resources[i].ptr_memfd_size = page_size;
					stress_set_vma_anon_name(resources[i].ptr_memfd, page_size, "resources-memfd");
					(void)stress_mincore_touch_pages_interruptible(resources[i].ptr_memfd, page_size);
					(void)stress_madvise_mergeable(resources[i].ptr_memfd, page_size);
				}
			}
			shim_fallocate(resources[i].fd_memfd, 0, 0, (off_t)stress_mwc16());
		}
		if (UNLIKELY(!stress_continue_flag()))
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
				else {
					resources[i].ptr_memfd_secret_size = page_size;
					stress_set_vma_anon_name(resources[i].ptr_memfd_secret, page_size, "resources-memfd-secret");
					(void)stress_mincore_touch_pages_interruptible(resources[i].ptr_memfd_secret, page_size);
					(void)stress_madvise_mergeable(resources[i].ptr_memfd_secret, page_size);
				}
			}
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
#endif
		resources[i].fd_sock = socket(
			domains[stress_mwc32modn((uint32_t)SIZEOF_ARRAY(domains))],
			types[stress_mwc32modn((uint32_t)SIZEOF_ARRAY(types))], 0);
		if (UNLIKELY(!stress_continue_flag()))
			break;

		if (socketpair(AF_UNIX, SOCK_STREAM, 0,
			resources[i].fd_socketpair) < 0) {
			resources[i].fd_socketpair[0] = -1;
			resources[i].fd_socketpair[1] = -1;
		}

#if defined(HAVE_USERFAULTFD)
		resources[i].fd_uf = shim_userfaultfd(0);
		if (UNLIKELY(!stress_continue_flag()))
			break;
#else
		UNEXPECTED
#endif
#if defined(O_TMPFILE)
		resources[i].fd_tmp = open("/tmp", O_TMPFILE | O_RDWR,
				      S_IRUSR | S_IWUSR);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (resources[i].fd_tmp != -1) {
			(void)shim_fallocate(resources[i].fd_tmp, 0, 0, (off_t)page_size);
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
				f.l_len = (off_t)page_size;
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
		if (UNLIKELY(!stress_continue_flag()))
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
		if (UNLIKELY(!stress_continue_flag()))
			break;
#endif

#if defined(HAVE_LIB_PTHREAD)
		if (!i) {
			resources[i].pthread_ret =
				pthread_create(&resources[i].pthread, NULL,
					stress_resources_pthread_func, NULL);
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		resources[i].mutex_ret = pthread_mutex_init(&resources[i].mutex, NULL);
		if (UNLIKELY(!stress_continue_flag()))
			break;
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_THREADS_H) &&		\
    defined(HAVE_MTX_T) &&		\
    defined(HAVE_MTX_DESTROY) &&	\
    defined(HAVE_MTX_INIT)
		resources[i].mtx_ret = mtx_init(&resources[i].mtx, mtx_plain);
		if (UNLIKELY(!stress_continue_flag()))
			break;
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
			if (UNLIKELY(!stress_continue_flag()))
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
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
#endif

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_SEM_POSIX)
		resources[i].semok = (sem_init(&resources[i].sem, 1, 1) >= 0);
		if (UNLIKELY(!stress_continue_flag()))
			break;
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_KEY_T)
		{
			/* Use even key so it won't clash with odd global sem key */
			const key_t sem_key = (key_t)stress_mwc32() & ~(key_t)1;

			resources[i].sem_id = semget(sem_key, 1,
				IPC_CREAT | S_IRUSR | S_IWUSR);
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
#endif

#if defined(HAVE_MQ_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H)
		resources[i].msgq_id = msgget(IPC_PRIVATE,
				S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
		if (UNLIKELY(!stress_continue_flag()))
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
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}
#endif
#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
		resources[i].pkey = shim_pkey_alloc(0, 0);
#endif
		if (UNLIKELY(!stress_continue_flag()))
			break;

#if defined(HAVE_PIDFD_OPEN)
		resources[i].pid_fd = shim_pidfd_open(ppid, 0);
#if defined(HAVE_PIDFD_GETFD)
		/* get parent pid stdout */
		resources[i].pid_fd_getfd = shim_pidfd_getfd(resources[i].pid_fd, 1, 0);
#endif
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
			(void)stress_munmap_force(resources[i].m_mmap, resources[i].m_mmap_size);
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
			(void)stress_munmap_force(resources[i].ptr_memfd, page_size);
			resources[i].ptr_memfd = MAP_FAILED;
		}
#endif
#if defined(__NR_memfd_secret)
		if (resources[i].fd_memfd_secret != -1) {
			(void)close(resources[i].fd_memfd_secret);
			resources[i].fd_memfd_secret = -1;
		}
		if (resources[i].ptr_memfd_secret) {
			(void)stress_munmap_force(resources[i].ptr_memfd_secret, page_size);
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
		}
#endif
#if defined(HAVE_LIB_PTHREAD) && 	\
    defined(HAVE_PTHREAD_MUTEX_T) &&	\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&	\
    defined(HAVE_PTHREAD_MUTEX_DESTROY)
		(void)pthread_mutex_destroy(&resources[i].mutex);
		(void)shim_memset(&resources[i].mutex, 0, sizeof(resources[i].mutex));
#endif
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_THREADS_H) &&		\
    defined(HAVE_MTX_T) &&		\
    defined(HAVE_MTX_DESTROY) &&	\
    defined(HAVE_MTX_INIT)
		if (resources[i].mtx_ret == 0) {
			mtx_destroy(&resources[i].mtx);
			(void)shim_memset(&resources[i].mtx, 0, sizeof(resources[i].mtx));
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
#if defined(HAVE_PIDFD_GETFD)
		if (resources[i].pid_fd_getfd > -1) {
			(void)close(resources[i].pid_fd_getfd);
			resources[i].pid_fd_getfd = -1;
		}
#endif
		}
#endif
		if (resources[i].pid > 0) {
			(void)stress_kill_pid_wait(resources[i].pid, NULL);
			resources[i].pid = 0;
		}
	}
}

/*
 *  stress_resources_access()
 *	access a wide range of resources
 */
void stress_resources_access(
	const stress_args_t *args,
	stress_resources_t *resources,
        const size_t num_resources)
{
	const size_t page_size = args->page_size;
	size_t i;

	(void)page_size;

	for (i = 0; i < num_resources; i++) {
		if (resources[i].m_malloc)
			(void)shim_memset(resources[i].m_malloc, (int)i, resources[i].m_malloc_size);
		if (resources[i].m_mmap && (resources[i].m_mmap != MAP_FAILED))
			(void)shim_memset(resources[i].m_mmap, (int)i, resources[i].m_mmap_size);
#if defined(F_GETFL)
		if (resources[i].pipe_ret != -1) {
			VOID_RET(int, fcntl(resources[i].fd_pipe[0], F_GETFL, 0));
			VOID_RET(int, fcntl(resources[i].fd_pipe[1], F_GETFL, 0));
		}
#endif
#if defined(F_GETFL)
		if (resources[i].fd_open != -1)
			VOID_RET(int, fcntl(resources[i].fd_open, F_GETFL, 0));
#endif

#if defined(HAVE_EVENTFD) && 	\
    defined(F_GETFL)
		if (resources[i].fd_ev != -1)
			VOID_RET(int, fcntl(resources[i].fd_ev, F_GETFL, 0));
#endif
#if defined(HAVE_MEMFD_CREATE)
#if defined(F_GETFL)
		if (resources[i].fd_memfd != -1)
			VOID_RET(int, fcntl(resources[i].fd_memfd, F_GETFL, 0));
#endif
		if (resources[i].ptr_memfd && (resources[i].ptr_memfd != MAP_FAILED))
			(void)shim_memset(resources[i].ptr_memfd, (int)i, resources[i].ptr_memfd_size);
#endif
#if defined(__NR_memfd_secret)
#if defined(F_GETFL)
		if (resources[i].fd_memfd_secret != -1)
			VOID_RET(int, fcntl(resources[i].fd_memfd_secret, F_GETFL, 0));
#endif
		if (resources[i].ptr_memfd_secret && (resources[i].ptr_memfd_secret != MAP_FAILED))
			(void)shim_memset(resources[i].ptr_memfd_secret, (int)i, resources[i].ptr_memfd_secret_size);
#endif
#if defined(F_GETFL)
		if (resources[i].fd_sock != -1)
			 VOID_RET(int, fcntl(resources[i].fd_sock, F_GETFL, 0));
#endif
#if defined(F_GETFL)
		if (resources[i].fd_socketpair[0] != -1)
			 VOID_RET(int, fcntl(resources[i].fd_socketpair[0], F_GETFL, 0));
		if (resources[i].fd_socketpair[1] != -1)
			 VOID_RET(int, fcntl(resources[i].fd_socketpair[1], F_GETFL, 0));
#endif
#if defined(HAVE_USERFAULTFD) &&	\
    defined(F_GETFL)
		if (resources[i].fd_uf != -1)
			 VOID_RET(int, fcntl(resources[i].fd_uf, F_GETFL, 0));
#endif
#if defined(O_TMPFILE) &&		\
    defined(F_GETFL)
		if (resources[i].fd_tmp != -1)
			 VOID_RET(int, fcntl(resources[i].fd_tmp, F_GETFL, 0));
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME) &&		\
    defined(F_GETFL)
		if ((!i) && (resources[i].timer_fd != -1))
			 VOID_RET(int, fcntl(resources[i].timer_fd, F_GETFL, 0));
#endif
#if defined(HAVE_SYS_INOTIFY) &&	\
    defined(F_GETFL)
		if (resources[i].wd_inotify != -1)
			VOID_RET(int, fcntl(resources[i].wd_inotify, F_GETFL, 0));
		if (resources[i].fd_inotify != -1)
			VOID_RET(int, fcntl(resources[i].fd_inotify, F_GETFL, 0));
#endif
#if defined(HAVE_PTSNAME) &&		\
    defined(F_GETFL)
		if (resources[i].pty != -1)
			VOID_RET(int, fcntl(resources[i].pty, F_GETFL, 0));
		if (resources[i].pty_mtx != -1)
			VOID_RET(int, fcntl(resources[i].pty_mtx, F_GETFL, 0));
#endif
#if defined(HAVE_PIDFD_OPEN) &&		\
    defined(F_GETFL)
		if (resources[i].pid_fd > -1)
			VOID_RET(int, fcntl(resources[i].pid_fd, F_GETFL, 0));
#if defined(HAVE_PIDFD_GETFD)
		if (resources[i].pid_fd_getfd > -1)
			VOID_RET(int, fcntl(resources[i].pid_fd_getfd, F_GETFL, 0));
#endif
#endif
		if (resources[i].pid > 0)
			(void)kill(resources[i].pid, 0);
	}
}
