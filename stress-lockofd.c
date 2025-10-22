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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL, "lockofd N",	 "start N workers using open file description locking" },
	{ NULL, "lockofd-ops N", "stop after N lockofd bogo operations" },
	{ NULL, NULL,		 NULL }
};

#if defined(F_OFD_GETLK) &&	\
    defined(F_OFD_SETLK) &&	\
    defined(F_OFD_SETLKW) && 	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)

#define LOCK_FILE_SIZE	(1024 * 1024)
#define LOCK_MAX	(1024)

typedef struct stress_lockofd_info {
	off_t	offset;
	off_t	len;
	struct stress_lockofd_info *next;
} stress_lockofd_info_t;

typedef struct {
	stress_lockofd_info_t *head;	/* Head of lockofd_info procs list */
	stress_lockofd_info_t *tail;	/* Tail of lockofd_info procs list */
	stress_lockofd_info_t *free;	/* List of free'd lockofd_infos */
	uint64_t length;		/* Length of list */
} stress_lockofd_info_list_t;

static stress_lockofd_info_list_t lockofd_infos;

/*
 *  stress_lockofd_info_new()
 *	allocate a new lockofd_info, add to end of list
 */
static stress_lockofd_info_t *stress_lockofd_info_new(void)
{
	stress_lockofd_info_t *new;

	if (lockofd_infos.free) {
		/* Pop an old one off the free list */
		new = lockofd_infos.free;
		lockofd_infos.free = new->next;
		new->next = NULL;
	} else {
		new = (stress_lockofd_info_t *)calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (lockofd_infos.head)
		lockofd_infos.tail->next = new;
	else
		lockofd_infos.head = new;

	lockofd_infos.tail = new;
	lockofd_infos.length++;

	return new;
}

/*
 *  stress_lockofd_info_head_remove
 *	reap a lockofd_info and remove a lockofd_info from head of list, put it onto
 *	the free lockofd_info list
 */
static void stress_lockofd_info_head_remove(void)
{
	if (lockofd_infos.head) {
		stress_lockofd_info_t *head = lockofd_infos.head;

		if (lockofd_infos.tail == lockofd_infos.head) {
			lockofd_infos.tail = NULL;
			lockofd_infos.head = NULL;
		} else {
			lockofd_infos.head = head->next;
		}

		/* Shove it on the free list */
		head->next = lockofd_infos.free;
		lockofd_infos.free = head;

		lockofd_infos.length--;
	}
}

/*
 *  stress_lockofd_info_free()
 *	free the lockofd_infos off the lockofd_info head and free lists
 */
static void stress_lockofd_info_free(void)
{
	while (lockofd_infos.head) {
		stress_lockofd_info_t *next = lockofd_infos.head->next;

		free(lockofd_infos.head);
		lockofd_infos.head = next;
	}

	while (lockofd_infos.free) {
		stress_lockofd_info_t *next = lockofd_infos.free->next;

		free(lockofd_infos.free);
		lockofd_infos.free = next;
	}
}

/*
 *  stress_lockofd_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_lockofd_unlock(stress_args_t *args, const int fd)
{
	struct flock f;

	/* Pop one off list */
	if (!lockofd_infos.head)
		return 0;

	f.l_type = F_UNLCK;
	f.l_whence = SEEK_SET;
	f.l_start = lockofd_infos.head->offset;
	f.l_len = lockofd_infos.head->len;
	f.l_pid = 0;

	stress_lockofd_info_head_remove();

	if (fcntl(fd, F_OFD_SETLK, &f) < 0) {
		pr_fail("%s: fcntl F_OFD_SETLK failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_lockofd_contention()
 *	hammer advisory lock/unlock to create some file lock contention
 */
static int stress_lockofd_contention(
	stress_args_t *args,
	const int fd)
{
	stress_mwc_reseed();

	do {
		off_t offset;
		off_t len;
		int rc;
		stress_lockofd_info_t *lockofd_info;
		struct flock f;

		if (lockofd_infos.length >= LOCK_MAX)
			if (stress_lockofd_unlock(args, fd) < 0)
				return -1;

		len = (stress_mwc16() + 1) & 0xfff;
		offset = (off_t)stress_mwc64modn((uint64_t)(LOCK_FILE_SIZE - len));

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = offset;
		f.l_len = len;
		f.l_pid = 0;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		rc = fcntl(fd, F_OFD_SETLK, &f);
		if (rc < 0)
			continue;

		/* Locked OK, add to lock list */

		lockofd_info = stress_lockofd_info_new();
		if (!lockofd_info) {
			pr_err("%s: calloc failed, out of memory%s\n",
				args->name, stress_get_memfree_str());
			return -1;
		}
		lockofd_info->offset = offset;
		lockofd_info->len = len;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_lockofd
 *	stress file locking via advisory locking
 */
static int stress_lockofd(stress_args_t *args)
{
	int fd, ret = EXIT_FAILURE, parent_cpu;
	pid_t cpid = -1;
	char filename[PATH_MAX];
	char pathname[PATH_MAX];
	char buffer[4096];
	off_t offset;
	ssize_t rc;

	(void)shim_memset(buffer, 0, sizeof(buffer));

	/*
	 *  There will be a race to create the directory
	 *  so EEXIST is expected on all but one instance
	 */
	(void)stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			ret = stress_exit_status(errno);
			pr_err("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return ret;
		}
	}

	/*
	 *  Lock file is based on parent pid and instance 0
	 *  as we need to share this among all the other
	 *  stress flock processes
	 */
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_err("%s: open %s failed, errno=%d (%s)\n",
			args->name, pathname, errno, strerror(errno));
		(void)shim_rmdir(pathname);
		return ret;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_err("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	for (offset = 0; offset < LOCK_FILE_SIZE; offset += sizeof(buffer)) {
redo:
		if (UNLIKELY(!stress_continue_flag())) {
			ret = EXIT_SUCCESS;
			goto tidy;
		}
		rc = write(fd, buffer, sizeof(buffer));
		if ((rc < 0) || (rc != sizeof(buffer))) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			ret = stress_exit_status(errno);
			pr_err("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	cpid = fork();
	if (cpid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto tidy;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	if (cpid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		if (stress_lockofd_contention(args, fd) < 0)
			_exit(EXIT_FAILURE);
		stress_lockofd_info_free();
		_exit(EXIT_SUCCESS);
	}

	if (stress_lockofd_contention(args, fd) == 0)
		ret = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (cpid > 1)
		stress_kill_and_wait(args, cpid, SIGALRM, true);
	stress_lockofd_info_free();

	(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return ret;
}

const stressor_info_t stress_lockofd_info = {
	.stressor = stress_lockofd,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_lockofd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fcntl() F_OFD_GETLK, F_OFD_SETLK, F_OFD_SETLKW, F_WRLCK or F_UNLCK commands"
};
#endif
