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

#include "stress-ng.h"

#if defined(STRESS_LOCKA)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#define LOCK_FILE_SIZE	(1024 * 1024)
#define LOCK_SIZE	(8)
#define LOCK_MAX	(1024)

typedef struct locka_info {
	off_t	offset;
	off_t	len;
	pid_t	pid;
	struct locka_info *next;
} locka_info_t;

typedef struct {
	locka_info_t *head;		/* Head of locka_info procs list */
	locka_info_t *tail;		/* Tail of locka_info procs list */
	locka_info_t *free;		/* List of free'd locka_infos */
	uint64_t length;		/* Length of list */
} locka_info_list_t;

static locka_info_list_t locka_infos;

/*
 *  stress_locka_info_new()
 *	allocate a new locka_info, add to end of list
 */
static locka_info_t *stress_locka_info_new(void)
{
	locka_info_t *new;

	if (locka_infos.free) {
		/* Pop an old one off the free list */
		new = locka_infos.free;
		locka_infos.free = new->next;
		new->next = NULL;
	} else {
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (locka_infos.head)
		locka_infos.tail->next = new;
	else
		locka_infos.head = new;

	locka_infos.tail = new;
	locka_infos.length++;

	return new;
}

/*
 *  stress_locka_info_head_remove
 *	reap a locka_info and remove a locka_info from head of list, put it onto
 *	the free locka_info list
 */
static void stress_locka_info_head_remove(void)
{
	if (locka_infos.head) {
		locka_info_t *head = locka_infos.head;

		if (locka_infos.tail == locka_infos.head) {
			locka_infos.tail = NULL;
			locka_infos.head = NULL;
		} else {
			locka_infos.head = head->next;
		}

		/* Shove it on the free list */
		head->next = locka_infos.free;
		locka_infos.free = head;

		locka_infos.length--;
	}
}

/*
 *  stress_locka_info_free()
 *	free the locka_infos off the locka_info head and free lists
 */
static void stress_locka_info_free(void)
{
	while (locka_infos.head) {
		locka_info_t *next = locka_infos.head->next;

		free(locka_infos.head);
		locka_infos.head = next;
	}

	while (locka_infos.free) {
		locka_info_t *next = locka_infos.free->next;

		free(locka_infos.free);
		locka_infos.free = next;
	}
}

/*
 *  stress_locka_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_locka_unlock(const char *name, const int fd)
{
	struct flock f;

	/* Pop one off list */
	if (!locka_infos.head)
		return 0;

	f.l_type = F_UNLCK;
	f.l_whence = SEEK_SET;
	f.l_start = locka_infos.head->offset;
	f.l_len = locka_infos.head->len;
	f.l_pid = locka_infos.head->pid;

	stress_locka_info_head_remove();

	if (fcntl(fd, F_SETLK, &f) < 0) {
		pr_fail_err(name, "F_SETLK");
		return -1;
	}
	return 0;
}

/*
 *  stress_locka_contention()
 *	hammer advisory lock/unlock to create some file lock contention
 */
static int stress_locka_contention(
	const char *name,
	const int fd,
	uint64_t *const counter,
	const uint64_t max_ops)
{
	mwc_reseed();
	pid_t pid = getpid();

	do {
		off_t offset;
		off_t len;
		int rc;
		locka_info_t *locka_info;
		struct flock f;

		if (locka_infos.length >= LOCK_MAX)
			if (stress_locka_unlock(name, fd) < 0)
				return -1;

		len  = (mwc16() + 1) & 0xfff;
		offset = mwc64() % (LOCK_FILE_SIZE - len);

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = offset;
		f.l_len = len;
		f.l_pid = pid;

		rc = fcntl(fd, F_GETLK, &f);
		if (rc < 0)
			continue;

		/* Locked OK, add to lock list */

		locka_info = stress_locka_info_new();
		if (!locka_info) {
			pr_fail_err(name, "calloc");
			return -1;
		}
		locka_info->offset = offset;
		locka_info->len = len;
		locka_info->pid = pid;

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return 0;
}

/*
 *  stress_locka
 *	stress file locking via advisory locking
 */
int stress_locka(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, ret = EXIT_FAILURE;
	pid_t pid = getpid(), cpid = -1;
	char filename[PATH_MAX];
	char dirname[PATH_MAX];
	char buffer[4096];
	off_t offset;
	ssize_t rc;

	memset(buffer, 0, sizeof(buffer));

	/*
	 *  There will be a race to create the directory
	 *  so EEXIST is expected on all but one instance
	 */
	(void)stress_temp_dir(dirname, sizeof(dirname), name, pid, instance);
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			ret = exit_status(errno);
			pr_fail_err(name, "mkdir");
			return ret;
		}
	}

	/*
	 *  Lock file is based on parent pid and instance 0
	 *  as we need to share this among all the other
	 *  stress flock processes
	 */
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err(name, "open");
		(void)rmdir(dirname);
		return ret;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail_err(name, "lseek");
		goto tidy;
	}
	for (offset = 0; offset < LOCK_FILE_SIZE; offset += sizeof(buffer)) {
redo:
		if (!opt_do_run) {
			ret = EXIT_SUCCESS;
			goto tidy;
		}
		rc = write(fd, buffer, sizeof(buffer));
		if ((rc < 0) || (rc != sizeof(buffer))) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			ret = exit_status(errno);
			pr_fail_err(name, "write");
			goto tidy;
		}
	}

again:
	cpid = fork();
	if (cpid < 0) {
		if (!opt_do_run) {
			ret = EXIT_SUCCESS;
			goto tidy;
		}
		if (errno == EAGAIN)
			goto again;
		pr_fail_err(name, "fork");
		goto tidy;
	}
	if (cpid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		if (stress_locka_contention(name, fd, counter, max_ops) < 0)
			exit(EXIT_FAILURE);
		stress_locka_info_free();
		exit(EXIT_SUCCESS);
	}
	setpgid(cpid, pgrp);

	if (stress_locka_contention(name, fd, counter, max_ops) == 0)
		ret = EXIT_SUCCESS;
tidy:
	if (cpid > 0) {
		int status;

		(void)kill(cpid, SIGKILL);
		(void)waitpid(cpid, &status, 0);
	}
	stress_locka_info_free();

	(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return ret;
}

#endif
