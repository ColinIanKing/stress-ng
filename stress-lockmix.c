/*
 * Copyright (C) 2024-2025 Colin Ian King.
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

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"lockmix N",	 "start N workers locking a file via flock, locka, lockf and ofd locks" },
	{ NULL,	"lockmix-ops N", "stop after N lockmix bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
#define HAVE_LOCKMIX_FLOCK
#endif

#if defined(F_GETLK) &&		\
    defined(F_SETLK) &&		\
    defined(F_SETLKW) &&	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
#define HAVE_LOCKMIX_LOCKA
#endif

#if defined(F_OFD_GETLK) &&	\
    defined(F_OFD_SETLK) &&	\
    defined(F_OFD_SETLKW) &&	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
#define HAVE_LOCKMIX_LOCKOFD
#endif

#if defined(HAVE_LOCKF) &&	\
    defined(F_LOCK) &&		\
    defined(F_ULOCK)
#define HAVE_LOCKMIX_LOCKF
#endif

#if defined(HAVE_LOCKMIX_FLOCK) ||	\
    defined(HAVE_LOCKMIX_LOCKA) ||	\
    defined(HAVE_LOCKMIX_LOCKF) ||	\
    defined(HAVE_LOCKMIX_LOCKOFD)

#define LOCK_FILE_SIZE	(1024 * 1024)
#define LOCK_MAX	(1024)
#define LOCK_SIZE	(8)

#define LOCKMIX_TYPE_FLOCK	(0)	/* flock */
#define LOCKMIX_TYPE_LOCKA	(1)	/* locka */
#define LOCKMIX_TYPE_LOCKF	(2)	/* lockf */
#define LOCKMIX_TYPE_LOCKOFD	(3)	/* lockofd */

typedef struct lockmix_info {
	struct lockmix_info *next;
	off_t	offset;
	off_t	len;
	pid_t	pid;
	uint8_t	type;
} stress_lockmix_info_t;

typedef struct {
	stress_lockmix_info_t *head;	/* Head of lockmix_info procs list */
	stress_lockmix_info_t *tail;	/* Tail of lockmix_info procs list */
	stress_lockmix_info_t *free;	/* List of free'd lockmix_infos */
	uint64_t length;		/* Length of list */
} stress_lockmix_info_list_t;

static stress_lockmix_info_list_t lockmix_infos;

static const char * const stress_lock_types[] = {
#if defined(HAVE_LOCKMIX_FLOCK)
	"flock",
#endif
#if defined(HAVE_LOCKMIX_LOCKA)
	"locka",
#endif
#if defined(HAVE_LOCKMIX_LOCKF)
	"lockf",
#endif
#if defined(HAVE_LOCKMIX_LOCKOFD)
	"ofd",
#endif
};

/*
 *  stress_lockmix_info_new()
 *	allocate a new lockmix_info, add to end of list
 */
static stress_lockmix_info_t *stress_lockmix_info_new(void)
{
	stress_lockmix_info_t *new;

	if (lockmix_infos.free) {
		/* Pop an old one off the free list */
		new = lockmix_infos.free;
		lockmix_infos.free = new->next;
		new->next = NULL;
	} else {
		new = (stress_lockmix_info_t *)calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (lockmix_infos.head)
		lockmix_infos.tail->next = new;
	else
		lockmix_infos.head = new;

	lockmix_infos.tail = new;
	lockmix_infos.length++;

	return new;
}

/*
 *  stress_lockmix_info_head_remove
 *	reap a lockmix_info and remove a lockmix_info from head of list, put it onto
 *	the free lockmix_info list
 */
static void stress_lockmix_info_head_remove(void)
{
	if (lockmix_infos.head) {
		stress_lockmix_info_t *head = lockmix_infos.head;

		if (lockmix_infos.tail == lockmix_infos.head) {
			lockmix_infos.tail = NULL;
			lockmix_infos.head = NULL;
		} else {
			lockmix_infos.head = head->next;
		}

		/* Shove it on the free list */
		head->next = lockmix_infos.free;
		lockmix_infos.free = head;

		lockmix_infos.length--;
	}
}

/*
 *  stress_lockmix_info_free()
 *	free the lockmix_infos off the lockmix_info head and free lists
 */
static void stress_lockmix_info_free(void)
{
	while (lockmix_infos.head) {
		stress_lockmix_info_t *next = lockmix_infos.head->next;

		free(lockmix_infos.head);
		lockmix_infos.head = next;
	}

	while (lockmix_infos.free) {
		stress_lockmix_info_t *next = lockmix_infos.free->next;

		free(lockmix_infos.free);
		lockmix_infos.free = next;
	}
}

/*
 *  stress_lockmix_unlock()
 *	pop oldest lock record off list and unlock it
 */
static int stress_lockmix_unlock(stress_args_t *args, const int fd)
{
#if defined(HAVE_LOCKMIX_LOCKA) ||	\
    defined(HAVE_LOCKMIX_LOCKOFD)
	struct flock f;
#endif
#if defined(HAVE_LOCKMIX_LOCKF)
	off_t offset;
#endif

	/* Pop one off list */
	if (UNLIKELY(!lockmix_infos.head))
		return 0;

	switch (lockmix_infos.head->type) {
#if defined(HAVE_LOCKMIX_FLOCK)
	case LOCKMIX_TYPE_FLOCK:
		stress_lockmix_info_head_remove();

		if (UNLIKELY(flock(fd, LOCK_UN) < 0)) {
			pr_fail("%s: flock LOCK_UN failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		break;
#endif

#if defined(HAVE_LOCKMIX_LOCKA)
	case LOCKMIX_TYPE_LOCKA:
		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = lockmix_infos.head->offset;
		f.l_len = lockmix_infos.head->len;
		f.l_pid = lockmix_infos.head->pid;

		stress_lockmix_info_head_remove();

		if (UNLIKELY(fcntl(fd, F_SETLK, &f) < 0)) {
			pr_fail("%s: fcntl F_SETLK failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		break;
#endif
#if defined(HAVE_LOCKMIX_LOCKF)
	case LOCKMIX_TYPE_LOCKF:
		offset = lockmix_infos.head->offset;

		stress_lockmix_info_head_remove();

		if (UNLIKELY(lseek(fd, offset, SEEK_SET) < 0)) {
			pr_err("%s: lseek failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}

		if (UNLIKELY(lockf(fd, F_ULOCK, LOCK_SIZE) < 0)) {
			pr_fail("%s: lockf F_ULOCK failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		break;
#endif
#if defined(HAVE_LOCKMIX_LOCKOFD)
	case LOCKMIX_TYPE_LOCKOFD:
		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = lockmix_infos.head->offset;
		f.l_len = lockmix_infos.head->len;
		f.l_pid = 0;

		stress_lockmix_info_head_remove();

		if (UNLIKELY(fcntl(fd, F_OFD_SETLK, &f) < 0)) {
			pr_fail("%s: fcntl F_OFD_SETLK failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		break;
#endif
	default:
		/* take off head anyhow */
		stress_lockmix_info_head_remove();
		break;
	}
	return 0;
}

/*
 *  stress_lockmix_contention()
 *	hammer advisory lock/unlock to create some file lock contention
 */
static int stress_lockmix_contention(
	stress_args_t *args,
	const int fd,
	const uint8_t *lock_types,
	const size_t lock_types_max)
{
	stress_mwc_reseed();

	do {
		off_t offset;
		off_t len;
#if defined(HAVE_LOCKMIX_LOCKA) ||	\
    defined(HAVE_LOCKMIX_LOCKF) ||	\
    defined(HAVE_LOCKMIX_LOCKOFD)
		int rc;
#endif
		uint8_t type;
		size_t type_idx;
		stress_lockmix_info_t *lockmix_info;
#if defined(HAVE_LOCKMIX_LOCKA) ||	\
    defined(HAVE_LOCKMIX_LOCKOFD)
		struct flock f;
#endif

		if (lockmix_infos.length >= LOCK_MAX)
			if (UNLIKELY(stress_lockmix_unlock(args, fd) < 0))
				return -1;

		len = (stress_mwc16() + 1) & 0xfff;
		offset = (off_t)stress_mwc64modn((uint64_t)(LOCK_FILE_SIZE - len));

		type_idx = (size_t)stress_mwc16modn(lock_types_max);
		type = lock_types[type_idx];

		switch (type) {
#if defined(HAVE_LOCKMIX_FLOCK)
		case LOCKMIX_TYPE_FLOCK:
			if (flock(fd, LOCK_EX) < 0)
				continue;
			break;
#endif
#if defined(HAVE_LOCKMIX_LOCKA)
		case LOCKMIX_TYPE_LOCKA:
			f.l_type = F_WRLCK;
			f.l_whence = SEEK_SET;
			f.l_start = offset;
			f.l_len = len;
			f.l_pid = args->pid;

			if (UNLIKELY(!stress_continue_flag()))
				break;
			rc = fcntl(fd, F_GETLK, &f);
			if (rc < 0)
				continue;
			break;
#endif
#if defined(HAVE_LOCKMIX_LOCKF)
		case LOCKMIX_TYPE_LOCKF:
			rc = lockf(fd, F_LOCK, LOCK_SIZE);
			if (rc < 0) {
				if (UNLIKELY(stress_lockmix_unlock(args, fd) < 0))
					return -1;
				continue;
			}
			break;
#endif
#if defined(HAVE_LOCKMIX_LOCKOFD)
		case LOCKMIX_TYPE_LOCKOFD:
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
			break;
#endif
		default:
			break;
		}

		/* Locked OK, add to lock list */

		lockmix_info = stress_lockmix_info_new();
		if (UNLIKELY(!lockmix_info)) {
			pr_err("%s: calloc failed, out of memory%s\n",
				args->name, stress_get_memfree_str());
			return -1;
		}
		lockmix_info->type = type;
		lockmix_info->offset = offset;
		lockmix_info->len = len;
		lockmix_info->pid = args->pid;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_lockmix
 *	stress file locking via advisory locking
 */
static int stress_lockmix(stress_args_t *args)
{
	int fd, ret = EXIT_FAILURE, parent_cpu;
	pid_t cpid = -1;
	char filename[PATH_MAX];
	char pathname[PATH_MAX];
	char buffer[4096];
	uint8_t lock_types[LOCK_MAX];
	off_t offset;
	ssize_t rc;
	size_t i, lock_types_max = 0, n;

	if (stress_instance_zero(args)) {
		(void)shim_memset(buffer, 0, sizeof(buffer));
		for (i = 0; i < SIZEOF_ARRAY(stress_lock_types); i++) {
			shim_strlcat(buffer, " ", sizeof(buffer));
			shim_strlcat(buffer, stress_lock_types[i], sizeof(buffer));
		}
		pr_inf("%s: exercising file lock type%s:%s\n", args->name,
			(SIZEOF_ARRAY(stress_lock_types) == 1) ? "" : "s",
			buffer);
	}

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

	(void)shim_memset(buffer, 0, sizeof(buffer));
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

#if defined(HAVE_LOCKMIX_FLOCK)
	n = LOCK_MAX / 64;
	for (i = 0; i < n; i++)
		lock_types[lock_types_max++] = LOCKMIX_TYPE_FLOCK;
#endif
#if defined(HAVE_LOCKMIX_LOCKF)
	n = LOCK_MAX / 64;
	for (i = 0; i < n; i++)
		lock_types[lock_types_max++] = LOCKMIX_TYPE_LOCKF;
#endif
#if defined(HAVE_LOCKMIX_LOCKA)
	n = (LOCK_MAX - lock_types_max) / 2;
	for (i = 0; i < n; i++)
		lock_types[lock_types_max++] = LOCKMIX_TYPE_LOCKA;
#endif
#if defined(HAVE_LOCKMIX_LOCKOFD)
	n = LOCK_MAX - lock_types_max;
	for (i = 0; i < n; i++)
		lock_types[lock_types_max++] = LOCKMIX_TYPE_LOCKOFD;
#endif
	for (i = 0; i < lock_types_max; i++) {
		uint8_t tmp;
		size_t j = (size_t)stress_mwc16modn(lock_types_max);

		tmp = lock_types[i];
		lock_types[i] = lock_types[j];
		lock_types[j] = tmp;
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

		if (stress_lockmix_contention(args, fd, lock_types, lock_types_max) < 0)
			_exit(EXIT_FAILURE);
		stress_lockmix_info_free();
		_exit(EXIT_SUCCESS);
	}

	if (stress_lockmix_contention(args, fd, lock_types, lock_types_max) == 0)
		ret = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (cpid > 1)
		stress_kill_and_wait(args, cpid, SIGALRM, true);
	stress_lockmix_info_free();

	(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return ret;
}
const stressor_info_t stress_lockmix_info = {
	.stressor = stress_lockmix,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_lockmix_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without flock, locka, lockf or ofd file locking support",
};
#endif
