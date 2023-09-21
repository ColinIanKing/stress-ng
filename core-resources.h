/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#ifndef CORE_RESOURCES_H
#define CORE_RESOURCES_H

#include "core-killpid.h"

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
	int padding1;
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
	int mutex_ret;
	pthread_mutex_t mutex;
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
#endif
} stress_resources_t;

extern size_t stress_resources_allocate(const stress_args_t *args, stress_resources_t *resources,
        const size_t num_resources, const size_t pipe_size, const size_t min_mem_free,
	const bool do_fork);
extern void stress_resources_free(const stress_args_t *args, stress_resources_t *resources,
        const size_t num_resources);

#endif
