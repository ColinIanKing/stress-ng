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

#ifndef CORE_RESOURCES_H
#define CORE_RESOURCES_H

#include <sys/socket.h>
#include "core-killpid.h"
#include "core-pthread.h"

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

#if defined(HAVE_THREADS_H)
#include <threads.h>
#endif

typedef struct {
	void *m_malloc;
	size_t m_malloc_size;
	void *m_sbrk;
	size_t m_sbrk_size;
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
	size_t ptr_memfd_size;
#endif
#if defined(__NR_memfd_secret)
	int fd_memfd_secret;
	int padding1;
	void *ptr_memfd_secret;
	size_t ptr_memfd_secret_size;
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
	int mutex_ret;
	pthread_mutex_t mutex;
#endif
#endif
#if defined(HAVE_LIB_PTHREAD) &&        \
    defined(HAVE_THREADS_H) &&          \
    defined(HAVE_MTX_T) &&              \
    defined(HAVE_MTX_DESTROY) &&        \
    defined(HAVE_MTX_INIT)
	int mtx_ret;
	mtx_t mtx;
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
	uint8_t padding[3];
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
#if defined(HAVE_PIDFD_GETFD)
	int pid_fd_getfd;
#endif
#endif
} stress_resources_t;

extern size_t stress_resources_allocate(stress_args_t *args, stress_resources_t *resources,
        const size_t num_resources, const size_t pipe_size, const size_t min_mem_free,
	const bool do_fork);
extern void stress_resources_access(const stress_args_t *args, stress_resources_t *resources,
        const size_t num_resources);
extern void stress_resources_free(const stress_args_t *args, stress_resources_t *resources,
        const size_t num_resources);

#endif
