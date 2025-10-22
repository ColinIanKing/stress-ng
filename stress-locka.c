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
	{ NULL,	"locka N",	"start N workers locking a file via advisory locks" },
	{ NULL,	"locka-ops N",	"stop after N locka bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(F_GETLK) && 	\
    defined(F_SETLK) &&		\
    defined(F_SETLKW) && 	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)

#define LOCK_FILE_SIZE	(1024 * 1024)
#define LOCK_MAX	(1024)

typedef struct locka_info {
	struct locka_info *next;
	off_t	offset;
	off_t	len;
	pid_t	pid;
} stress_locka_info_t;

typedef struct {
	stress_locka_info_t *head;	/* Head of locka_info procs list */
	stress_locka_info_t *tail;	/* Tail of locka_info procs list */
	stress_locka_info_t *free;	/* List of free'd locka_infos */
	uint64_t length;		/* Length of list */
} stress_locka_info_list_t;

static stress_locka_info_list_t locka_infos;

/*
 *  stress_locka_info_new()
 *	allocate a new locka_info, add to end of list
 */
static stress_locka_info_t *stress_locka_info_new(void)
{
	stress_locka_info_t *new;

	if (locka_infos.free) {
		/* Pop an old one off the free list */
		new = locka_infos.free;
		locka_infos.free = new->next;
		new->next = NULL;
	} else {
		new = (stress_locka_info_t *)calloc(1, sizeof(*new));
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
		stress_locka_info_t *head = locka_infos.head;

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
		stress_locka_info_t *next = locka_infos.head->next;

		free(locka_infos.head);
		locka_infos.head = next;
	}

	while (locka_infos.free) {
		stress_locka_info_t *next = locka_infos.free->next;

		free(locka_infos.free);
		locka_infos.free = next;
	}
}

/*
 *  stress_locka_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_locka_unlock(stress_args_t *args, const int fd)
{
	struct flock f;

	/* Pop one off list */
	if (UNLIKELY(!locka_infos.head))
		return 0;

	f.l_type = F_UNLCK;
	f.l_whence = SEEK_SET;
	f.l_start = locka_infos.head->offset;
	f.l_len = locka_infos.head->len;
	f.l_pid = locka_infos.head->pid;

	stress_locka_info_head_remove();

	if (UNLIKELY(fcntl(fd, F_SETLK, &f) < 0)) {
		pr_fail("%s: fcntl F_SETLK failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_locka_contention()
 *	hammer advisory lock/unlock to create some file lock contention
 */
static int stress_locka_contention(
	stress_args_t *args,
	const int fd)
{
	stress_mwc_reseed();

	do {
		off_t offset;
		off_t len;
		int rc;
		stress_locka_info_t *locka_info;
		struct flock f;

		if (locka_infos.length >= LOCK_MAX)
			if (UNLIKELY(stress_locka_unlock(args, fd) < 0))
				return -1;

		len = (stress_mwc16() + 1) & 0xfff;
		offset = (off_t)stress_mwc64modn((uint64_t)(LOCK_FILE_SIZE - len));

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = offset;
		f.l_len = len;
		f.l_pid = args->pid;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		rc = fcntl(fd, F_GETLK, &f);
		if (UNLIKELY(rc < 0))
			continue;

		/* Locked OK, add to lock list */

		locka_info = stress_locka_info_new();
		if (UNLIKELY(!locka_info)) {
			pr_err("%s: calloc failed, out of memory%s\n",
				args->name, stress_get_memfree_str());
			return -1;
		}
		locka_info->offset = offset;
		locka_info->len = len;
		locka_info->pid = args->pid;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_locka
 *	stress file locking via advisory locking
 */
static int stress_locka(stress_args_t *args)
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
			args->name, filename, errno, strerror(errno));
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

		if (stress_locka_contention(args, fd) < 0)
			_exit(EXIT_FAILURE);
		stress_locka_info_free();
		_exit(EXIT_SUCCESS);
	}

	if (stress_locka_contention(args, fd) == 0)
		ret = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (cpid > 1)
		stress_kill_and_wait(args, cpid, SIGALRM, true);
	stress_locka_info_free();

	(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return ret;
}
const stressor_info_t stress_locka_info = {
	.stressor = stress_locka,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_locka_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fcntl() F_GETLK, F_SETLK, F_SETLKW, F_WRLCK or F_UNLCK commands"
};
#endif
