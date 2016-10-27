/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#if defined(__NR_eventfd)
#include <sys/eventfd.h>
#endif
#if defined(__sun__)
#include <alloca.h>
#endif

#include "stress-ng.h"

#define RESOURCE_FORKS 	(1024)
#define MAX_LOOPS	(1024)

typedef struct {
	void *m_sbrk;
	void *m_alloca;
	void *m_mmap;
	size_t m_mmap_size;
	int fd_pipe[2];
	int pipe_ret;
	int fd_open;
#if defined(__NR_eventfd)
	int fd_ev;
#endif
#if defined(__NR_memfd_create)
	int fd_memfd;
#endif
	int fd_sock;
#if defined(__NR_userfaultfd)
	int fd_uf;
#endif
#if defined(O_TMPFILE)
	int fd_tmp;
#endif
} info_t;

static pid_t pids[RESOURCE_FORKS];
static sigjmp_buf jmp_env;

#if defined(__NR_memfd_create)
static inline int sys_memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}
#endif

#if defined(__NR_userfaultfd)
static inline int sys_userfaultfd(int flags)
{
        return syscall(__NR_userfaultfd, flags);
}
#endif

static void waste_resources(const size_t page_size)
{
	size_t i;
#if defined(__NR_memfd_create) || defined(O_TMPFILE)
	const pid_t pid = getpid();
#endif

	static int domains[] = { AF_INET, AF_INET6 };
	static int types[] = { SOCK_STREAM, SOCK_DGRAM };
	info_t info[MAX_LOOPS];

	memset(&info, 0, sizeof(info));

	for (i = 0; opt_do_run && (i < MAX_LOOPS); i++) {
#if defined(__NR_memfd_create)
		char name[32];
#endif
		if (!(mwc32() & 0xf)) {
			info[i].m_sbrk = sbrk(page_size);
			if (!opt_do_run)
				break;
		}
		if (!(mwc32() & 0xf)) {
			info[i].m_alloca = alloca(page_size);
			if (!opt_do_run)
				break;
		}
		if (!(mwc32() & 0xf)) {
			info[i].m_mmap_size = page_size;
			info[i].m_mmap = mmap(NULL, info[i].m_mmap_size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
			if (!opt_do_run)
				break;
			if (info[i].m_mmap != MAP_FAILED) {
				mincore_touch_pages(info[i].m_mmap, info[i].m_mmap_size);
				if (!opt_do_run)
					break;
			}
		}
		info[i].pipe_ret = pipe(info[i].fd_pipe);
		if (!opt_do_run)
			break;
		info[i].fd_open = open("/dev/null", O_RDONLY);
		if (!opt_do_run)
			break;
#if defined(__NR_eventfd)
		info[i].fd_ev = eventfd(0, 0);
		if (!opt_do_run)
			break;
#endif
#if defined(__NR_memfd_create)
		snprintf(name, sizeof(name), "memfd-%u-%zu", pid, i);
		info[i].fd_memfd = sys_memfd_create(name, 0);
		if (!opt_do_run)
			break;
#endif
		info[i].fd_sock = socket(
			domains[mwc32() % SIZEOF_ARRAY(domains)],
			types[mwc32() % SIZEOF_ARRAY(types)], 0);
		if (!opt_do_run)
			break;
#if defined(__NR_userfaultfd)
		info[i].fd_uf = sys_userfaultfd(0);
		if (!opt_do_run)
			break;
#endif
#if defined(O_TMPFILE)
		info[i].fd_tmp = open("/tmp", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
		if (!opt_do_run)
			break;
		if (info[i].fd_tmp != -1) {
			size_t sz = page_size * mwc32();

			fallocate(info[i].fd_tmp, 0, 0, sz);
#if defined(F_GETLK) && defined(F_SETLK) && defined(F_SETLKW) && \
    defined(F_WRLCK) && defined(F_UNLCK)
			{
				struct flock f;

				f.l_type = F_WRLCK;
				f.l_whence = SEEK_SET;
				f.l_start = 0;
				f.l_len = sz;
				f.l_pid = pid;

				(void)fcntl(info[i].fd_tmp, F_SETLK, &f);
			}
		}
#endif
#endif
	}

	for (i = 0; opt_do_run && (i < MAX_LOOPS); i++) {
		if (info[i].m_mmap && (info[i].m_mmap != MAP_FAILED))
			(void)munmap(info[i].m_mmap, info[i].m_mmap_size);
		if (info[i].pipe_ret != -1) {
			(void)close(info[i].fd_pipe[0]);
			(void)close(info[i].fd_pipe[1]);
		}
		if (info[i].fd_open != -1)
			(void)close(info[i].fd_open);
#if defined(__NR_eventfd)
		if (info[i].fd_ev != -1)
			(void)close(info[i].fd_ev);
#endif
#if defined(__NR_memfd_create)
		if (info[i].fd_memfd != -1)
			(void)close(info[i].fd_memfd);
#endif
		if (info[i].fd_sock != -1)
			(void)close(info[i].fd_sock);
#if defined(__NR_userfaultfd)
		if (info[i].fd_uf != -1)
			(void)close(info[i].fd_uf);
#endif
#if defined(O_TMPFILE)
		if (info[i].fd_tmp != -1)
			(void)close(info[i].fd_tmp);
#endif
	}
}

static void MLOCKED kill_children(void)
{
	size_t i;

	for (i = 0; i < RESOURCE_FORKS; i++) {
		if (pids[i])
			kill(pids[i], SIGKILL);
	}

	for (i = 0; i < RESOURCE_FORKS; i++) {
		if (pids[i]) {
			int status;

			(void)waitpid(pids[i], &status, 0);
		}
	}
}

static void MLOCKED stress_alrmhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_resources()
 *	stress by forking and exiting
 */
int stress_resources(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	int ret;

	(void)instance;

	if (stress_sighandler(name, SIGALRM, stress_alrmhandler, NULL) < 0)
		return EXIT_FAILURE;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		kill_children();
		return EXIT_SUCCESS;
	}

	do {
		unsigned int i;

		memset(pids, 0, sizeof(pids));
		for (i = 0; i < RESOURCE_FORKS; i++) {
			pid_t pid = fork();

			if (pid == 0) {
				(void)setpgid(0, pgrp);
				ret = sigsetjmp(jmp_env, 1);
				if (ret)
					_exit(0);
				set_oom_adjustment(name, true);
				waste_resources(page_size);
				_exit(0);
			}
			if (pid > -1)
				(void)setpgid(pids[i], pgrp);
			pids[i] = pid;

			if (!opt_do_run) {
				kill_children();
				return EXIT_SUCCESS;
			}
		}
		kill_children();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
