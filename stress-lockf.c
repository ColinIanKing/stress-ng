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
	{ NULL,	"lockf N",	  "start N workers locking a single file via lockf" },
	{ NULL, "lockf-nonblock", "don't block if lock cannot be obtained, re-try" },
	{ NULL,	"lockf-ops N",	  "stop after N lockf bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_LOCKF)

#define LOCK_FILE_SIZE	(64 * 1024)
#define LOCK_SIZE	(8)
#define LOCK_MAX	(1024)

typedef struct lockf_info {
	off_t	offset;
	struct lockf_info *next;
} stress_lockf_info_t;

typedef struct {
	stress_lockf_info_t *head;	/* Head of lockf_info procs list */
	stress_lockf_info_t *tail;	/* Tail of lockf_info procs list */
	stress_lockf_info_t *free;	/* List of free'd lockf_infos */
	uint64_t length;		/* Length of list */
} stress_lockf_info_list_t;

static stress_lockf_info_list_t lockf_infos;
#endif

static const stress_opt_t opts[] = {
	{ OPT_lockf_nonblock, "lockf-nonblock", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_LOCKF)
/*
 *  stress_lockf_info_new()
 *	allocate a new lockf_info, add to end of list
 */
static stress_lockf_info_t *stress_lockf_info_new(void)
{
	stress_lockf_info_t *new;

	if (lockf_infos.free) {
		/* Pop an old one off the free list */
		new = lockf_infos.free;
		lockf_infos.free = new->next;
		new->next = NULL;
	} else {
		new = (stress_lockf_info_t *)calloc(1, sizeof(*new));
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
		stress_lockf_info_t *head = lockf_infos.head;

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
		stress_lockf_info_t *next = lockf_infos.head->next;

		free(lockf_infos.head);
		lockf_infos.head = next;
	}

	while (lockf_infos.free) {
		stress_lockf_info_t *next = lockf_infos.free->next;

		free(lockf_infos.free);
		lockf_infos.free = next;
	}
}

/*
 *  stress_lockf_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_lockf_unlock(stress_args_t *args, const int fd)
{
	/* Pop one off list */
	if (UNLIKELY(!lockf_infos.head))
		return 0;

	if (UNLIKELY(lseek(fd, lockf_infos.head->offset, SEEK_SET) < 0)) {
		pr_err("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	stress_lockf_info_head_remove();

	if (UNLIKELY(lockf(fd, F_ULOCK, LOCK_SIZE) < 0)) {
		pr_fail("%s: lockf F_ULOCK failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_lockf_contention()
 *	hammer lock/unlock to create some file lock contention
 */
static int stress_lockf_contention(
	stress_args_t *args,
	const int fd,
	const int bad_fd)
{
	bool lockf_nonblock = false;
	int lockf_cmd;
	int counter = 0;

	(void)stress_get_setting("lockf-nonblock", &lockf_nonblock);
	lockf_cmd = lockf_nonblock ?  F_TLOCK : F_LOCK;
	stress_mwc_reseed();

	do {
		off_t offset;
		int rc;
		stress_lockf_info_t *lockf_info;

		if (lockf_infos.length >= LOCK_MAX)
			if (stress_lockf_unlock(args, fd) < 0)
				return -1;

		offset = (off_t)stress_mwc64modn(LOCK_FILE_SIZE - LOCK_SIZE);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(lseek(fd, offset, SEEK_SET) < 0)) {
			pr_err("%s: lseek failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
		rc = lockf(fd, lockf_cmd, LOCK_SIZE);
		if (UNLIKELY(rc < 0)) {
			if (stress_lockf_unlock(args, fd) < 0)
				return -1;
			continue;
		}

		/* Locked OK, add to lock list */
		lockf_info = stress_lockf_info_new();
		if (UNLIKELY(!lockf_info)) {
			pr_err("%s: calloc failed, out of memory%s\n",
				args->name, stress_get_memfree_str());
			return -1;
		}
		lockf_info->offset = offset;
		/*
		 *  Occasionally exercise lock on a bad fd, ignore error
		 */
		if (UNLIKELY(counter++ >= 65536)) {
			if (UNLIKELY(!stress_continue_flag()))
				break;
			VOID_RET(int, lockf(bad_fd, lockf_cmd, LOCK_SIZE));
			counter = 0;

			if (UNLIKELY(!stress_continue_flag()))
				break;
			/* Exercise F_TEST, ignore result */
			VOID_RET(int, lockf(fd, F_TEST, LOCK_SIZE));
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_lockf
 *	stress file locking via lockf()
 */
static int stress_lockf(stress_args_t *args)
{
	int fd, ret = EXIT_FAILURE, parent_cpu;
	const int bad_fd = stress_get_bad_fd();
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

		if (stress_lockf_contention(args, fd, bad_fd) < 0)
			_exit(EXIT_FAILURE);
		stress_lockf_info_free();
		_exit(EXIT_SUCCESS);
	}

	if (stress_lockf_contention(args, fd, bad_fd) == 0)
		ret = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (cpid > 1)
		stress_kill_and_wait(args, cpid, SIGALRM, true);
	stress_lockf_info_free();

	(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return ret;
}

const stressor_info_t stress_lockf_info = {
	.stressor = stress_lockf,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_lockf_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without lockf() system call"
};
#endif
