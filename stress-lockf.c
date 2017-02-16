/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)

#define LOCK_FILE_SIZE	(64 * 1024)
#define LOCK_SIZE	(8)
#define LOCK_MAX	(1024)

typedef struct lockf_info {
	off_t	offset;
	struct lockf_info *next;
} lockf_info_t;

typedef struct {
	lockf_info_t *head;		/* Head of lockf_info procs list */
	lockf_info_t *tail;		/* Tail of lockf_info procs list */
	lockf_info_t *free;		/* List of free'd lockf_infos */
	uint64_t length;		/* Length of list */
} lockf_info_list_t;

static lockf_info_list_t lockf_infos;

/*
 *  stress_lockf_info_new()
 *	allocate a new lockf_info, add to end of list
 */
static lockf_info_t *stress_lockf_info_new(void)
{
	lockf_info_t *new;

	if (lockf_infos.free) {
		/* Pop an old one off the free list */
		new = lockf_infos.free;
		lockf_infos.free = new->next;
		new->next = NULL;
	} else {
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (lockf_infos.head)
		lockf_infos.tail->next = new;
	else
		lockf_infos.head = new;

	lockf_infos.tail = new;
	lockf_infos.length++;

	return new;
}

/*
 *  stress_lockf_info_head_remove
 *	reap a lockf_info and remove a lockf_info from head of list, put it onto
 *	the free lockf_info list
 */
static void stress_lockf_info_head_remove(void)
{
	if (lockf_infos.head) {
		lockf_info_t *head = lockf_infos.head;

		if (lockf_infos.tail == lockf_infos.head) {
			lockf_infos.tail = NULL;
			lockf_infos.head = NULL;
		} else {
			lockf_infos.head = head->next;
		}

		/* Shove it on the free list */
		head->next = lockf_infos.free;
		lockf_infos.free = head;

		lockf_infos.length--;
	}
}

/*
 *  stress_lockf_info_free()
 *	free the lockf_infos off the lockf_info head and free lists
 */
static void stress_lockf_info_free(void)
{
	while (lockf_infos.head) {
		lockf_info_t *next = lockf_infos.head->next;

		free(lockf_infos.head);
		lockf_infos.head = next;
	}

	while (lockf_infos.free) {
		lockf_info_t *next = lockf_infos.free->next;

		free(lockf_infos.free);
		lockf_infos.free = next;
	}
}

/*
 *  stress_lockf_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_lockf_unlock(const args_t *args, const int fd)
{
	/* Pop one off list */
	if (!lockf_infos.head)
		return 0;

	if (lseek(fd, lockf_infos.head->offset, SEEK_SET) < 0) {
		pr_fail_err("lseek");
		return -1;
	}
	stress_lockf_info_head_remove();

	if (lockf(fd, F_ULOCK, LOCK_SIZE) < 0) {
		pr_fail_err("lockf unlock");
		return -1;
	}
	return 0;
}

/*
 *  stress_lockf_contention()
 *	hammer lock/unlock to create some file lock contention
 */
static int stress_lockf_contention(
	const args_t *args,
	const int fd)
{
	const int lockf_cmd = (g_opt_flags & OPT_FLAGS_LOCKF_NONBLK) ?
		F_TLOCK : F_LOCK;

	mwc_reseed();

	do {
		off_t offset;
		int rc;
		lockf_info_t *lockf_info;

		if (lockf_infos.length >= LOCK_MAX)
			if (stress_lockf_unlock(args, fd) < 0)
				return -1;

		offset = mwc64() % (LOCK_FILE_SIZE - LOCK_SIZE);
		if (lseek(fd, offset, SEEK_SET) < 0) {
			pr_fail_err("lseek");
			return -1;
		}
		rc = lockf(fd, lockf_cmd, LOCK_SIZE);
		if (rc < 0) {
			if (stress_lockf_unlock(args, fd) < 0)
				return -1;
			continue;
		}
		/* Locked OK, add to lock list */

		lockf_info = stress_lockf_info_new();
		if (!lockf_info) {
			pr_fail_err("calloc");
			return -1;
		}
		lockf_info->offset = offset;
		inc_counter(args);
	} while (keep_stressing());

	return 0;
}

/*
 *  stress_lockf
 *	stress file locking via lockf()
 */
int stress_lockf(const args_t *args)
{
	int fd, ret = EXIT_FAILURE;
	pid_t cpid = -1;
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
	(void)stress_temp_dir_args(args, dirname, sizeof(dirname));
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			ret = exit_status(errno);
			pr_fail_err("mkdir");
			return ret;
		}
	}

	/*
	 *  Lock file is based on parent pid and instance 0
	 *  as we need to share this among all the other
	 *  stress flock processes
	 */
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err("open");
		(void)rmdir(dirname);
		return ret;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_fail_err("lseek");
		goto tidy;
	}
	for (offset = 0; offset < LOCK_FILE_SIZE; offset += sizeof(buffer)) {
redo:
		if (!g_keep_stressing_flag) {
			ret = EXIT_SUCCESS;
			goto tidy;
		}
		rc = write(fd, buffer, sizeof(buffer));
		if ((rc < 0) || (rc != sizeof(buffer))) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			ret = exit_status(errno);
			pr_fail_err("write");
			goto tidy;
		}
	}

again:
	cpid = fork();
	if (cpid < 0) {
		if (!g_keep_stressing_flag) {
			ret = EXIT_SUCCESS;
			goto tidy;
		}
		if (errno == EAGAIN)
			goto again;
		pr_fail_err("fork");
		goto tidy;
	}
	if (cpid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		if (stress_lockf_contention(args, fd) < 0)
			exit(EXIT_FAILURE);
		stress_lockf_info_free();
		exit(EXIT_SUCCESS);
	}
	(void)setpgid(cpid, g_pgrp);

	if (stress_lockf_contention(args, fd) == 0)
		ret = EXIT_SUCCESS;
tidy:
	if (cpid > 0) {
		int status;

		(void)kill(cpid, SIGKILL);
		(void)waitpid(cpid, &status, 0);
	}
	stress_lockf_info_free();

	(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return ret;
}
#else
int stress_lockf(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
