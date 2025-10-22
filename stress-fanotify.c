/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
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
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-mounts.h"

#include <sys/ioctl.h>

#if defined(HAVE_SYS_FANOTIFY_H)
#include <sys/fanotify.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"fanotify N",	  "start N workers exercising fanotify events" },
	{ NULL,	"fanotify-ops N", "stop fanotify workers after N bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_MNTENT_H) &&		\
    defined(HAVE_SYS_SELECT_H) && 	\
    defined(HAVE_SYS_FANOTIFY_H) &&	\
    defined(HAVE_FANOTIFY)

#define MAX_MNTS	(4096)

#define BUFFER_SIZE	(4096)

/* fanotify stats */
typedef struct {
	uint64_t open;		/* # opens */
	uint64_t close_write;	/* # close writes */
	uint64_t close_nowrite;	/* # close no writes */
	uint64_t access;	/* # accesses */
	uint64_t modify;	/* # modifications */
	uint64_t rename;	/* # renames */
} stress_fanotify_account_t;

#endif

#if defined(HAVE_FANOTIFY) &&	\
    defined(HAVE_SYS_SELECT_H)

static const unsigned int fan_stress_settings[] = {
#if defined(FAN_ACCESS)
	FAN_ACCESS,
#endif
#if defined(FAN_ACCESS_PERM)
	FAN_ACCESS_PERM,
#endif
#if defined(FAN_ATTRIB)
	FAN_ATTRIB,
#endif
#if defined(FAN_CLOSE)
	FAN_CLOSE,
#endif
#if defined(FAN_CLOSE_NOWRITE)
	FAN_CLOSE_NOWRITE,
#endif
#if defined(FAN_CLOSE_WRITE)
	FAN_CLOSE_WRITE,
#endif
#if defined(FAN_CREATE)
	FAN_CREATE,
#endif
#if defined(FAN_DELETE)
	FAN_DELETE,
#endif
#if defined(FAN_DELETE_SELF)
	FAN_DELETE_SELF,
#endif
#if defined(FAN_DIR_MODIFY)
	FAN_DIR_MODIFY,
#endif
#if defined(FAN_EVENT_ON_CHILD)
	FAN_EVENT_ON_CHILD,
#endif
#if defined(FAN_MODIFY)
	FAN_MODIFY,
#endif
#if defined(FAN_MOVE)
	FAN_MOVE,
#endif
#if defined(FAN_MOVED_FROM)
	FAN_MOVED_FROM,
#endif
#if defined(FAN_MOVE_SELF)
	FAN_MOVE_SELF,
#endif
#if defined(FAN_MOVED_TO)
	FAN_MOVED_TO,
#endif
#if defined(FAN_ONDIR)
	FAN_ONDIR,
#endif
#if defined(FAN_OPEN)
	FAN_OPEN,
#endif
#if defined(FAN_OPEN_EXEC)
	FAN_OPEN_EXEC,
#endif
#if defined(FAN_OPEN_EXEC_PERM)
	FAN_OPEN_EXEC_PERM,
#endif
#if defined(FAN_OPEN_PERM)
	FAN_OPEN_PERM,
#endif
#if defined(FAN_Q_OVERFLOW)
	FAN_Q_OVERFLOW,
#endif
#if defined(FAN_RENAME)
	FAN_RENAME,
#endif
#if defined(FAN_FS_ERROR)
	FAN_FS_ERROR,
#endif
	0
};

static const unsigned int init_flags[] =
{
#if defined(FAN_CLASS_CONTENT)
	FAN_CLASS_CONTENT,
#endif
#if defined(FAN_CLASS_PRE_CONTENT)
	FAN_CLASS_PRE_CONTENT,
#endif
#if defined(FAN_UNLIMITED_QUEUE)
	FAN_UNLIMITED_QUEUE,
#endif
#if defined(FAN_UNLIMITED_MARKS)
	FAN_UNLIMITED_MARKS,
#endif
#if defined(FAN_CLOEXEC)
	FAN_CLOEXEC,
#endif
#if defined(FAN_NONBLOCK)
	FAN_NONBLOCK,
#endif
#if defined(FAN_ENABLE_AUDIT)
	FAN_ENABLE_AUDIT,
#endif
#if defined(FAN_REPORT_NAME)
	FAN_REPORT_NAME,
#endif
#if defined(FAN_REPORT_TID)
	FAN_REPORT_TID,
#endif
#if defined(FAN_REPORT_FID)
	FAN_REPORT_FID,
#endif
#if defined(FAN_REPORT_DIR_FID)
	FAN_REPORT_DIR_FID,
#endif
#if defined(FAN_REPORT_PIDFD)
	FAN_REPORT_PIDFD,
#endif
#if defined(FAN_REPORT_TARGET_FID)
	FAN_REPORT_TARGET_FID,
#endif
#if defined(FAN_REPORT_FD_ERROR)
	FAN_REPORT_FD_ERROR,
#endif
#if defined(FAN_REPORT_MNT)
	FAN_REPORT_MNT,
#endif
};

static char *mnts[MAX_MNTS];
static int n_mnts;

/*
 *  stress_fanotify_supported()
 *      check if we can run this as root
 */
static int stress_fanotify_supported(const char *name)
{
	int fan_fd;
	static const char skipped[] =
		"stressor will be skipped, ";
	static const char noperm[] =
		"stressor needs to be running with CAP_SYS_ADMIN "
		"rights";
	static const char noresource[] =
		": no resources (out of descriptors or memory)";
	static const char nosyscall[] =
		": system call not supported";

	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s %s%s\n", name, skipped, noperm);
		return -1;
	}
	fan_fd = fanotify_init(0, 0);
	if (fan_fd < 0) {
		int rc = -1;

		switch (errno) {
		case EPERM:
			pr_inf_skip("%s %s%s\n", name, skipped, noperm);
			break;
		case EMFILE:
		case ENOMEM:
			pr_inf_skip("%s %s%s\n", name, skipped, noresource);
			break;
		case ENOSYS:
			pr_inf_skip("%s %s%s\n", name, skipped, nosyscall);
			break;
		default:
			rc = 0;
			break;
		}
		return rc;
	}
	(void)close(fan_fd);

	return 0;
}

/*
 *  fanotify_event_init_invalid_call()
 *	perform init call and close fd if call succeeded (which should
 *	not happen).
 */
static void fanotify_event_init_invalid_call(unsigned int flags, unsigned int event_f_flags)
{
	int fan_fd;

	fan_fd = fanotify_init(flags, event_f_flags);
	if (fan_fd >= 0)
		(void)close(fan_fd);
}

#define FANOTIFY_CLASS_BITS

/*
 *  fanotify_event_init_invalid()
 *  exercise invalid ways to call fanotify_init to
 *  get more kernel coverage
 */
static void fanotify_event_init_invalid(void)
{
	fanotify_event_init_invalid_call(0U, ~0U);
	fanotify_event_init_invalid_call(~0U, ~0U);
	fanotify_event_init_invalid_call(~0U, 0U);

#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_CLASS_CONTENT) && 	\
    defined(FAN_CLASS_PRE_CONTENT)
	fanotify_event_init_invalid_call(FAN_CLASS_NOTIF |
					 FAN_CLASS_CONTENT |
					 FAN_CLASS_PRE_CONTENT, ~0U);
#endif
}

/*
 *  test_fanotify_mark()
 *     tests fanotify_mark syscall
 */
static int test_fanotify_mark(char *mounts[])
{
	int ret_fd;
#if defined(FAN_MARK_INODE)
	const int bad_fd = stress_get_bad_fd();
#endif

	ret_fd = fanotify_init(0, 0);
	if (ret_fd < 0)
		return -errno;

	/* Exercise fanotify_mark with invalid mask */
#if defined(FAN_MARK_MOUNT)
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
			~0U, AT_FDCWD, mounts[0]));
#endif

	/* Exercise fanotify_mark with invalid flag */
	VOID_RET(int, fanotify_mark(ret_fd, ~0U, FAN_ACCESS, AT_FDCWD, mounts[0]));

	/* Exercise fanotify_mark on bad fd */
#if defined(FAN_MARK_INODE)
	VOID_RET(int, fanotify_mark(bad_fd, FAN_MARK_ADD | FAN_MARK_INODE,
		FAN_ACCESS, AT_FDCWD, mounts[0]));
#endif

	/* Exercise fanotify_mark by passing two operations simultaneously */
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_REMOVE | FAN_MARK_ADD,
		FAN_ACCESS, AT_FDCWD, mounts[0]));

	/* Exercise valid fanotify_mark to increase kernel coverage */
#if defined(FAN_MARK_INODE)
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_ADD | FAN_MARK_INODE,
		FAN_ACCESS, AT_FDCWD, mounts[0]));
#endif

#if defined(FAN_MARK_IGNORED_MASK)
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_ADD | FAN_MARK_IGNORED_MASK,
		FAN_ACCESS, AT_FDCWD, mounts[0]));
#endif

	/* Exercise other invalid combinations of flags */
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_REMOVE,
		0, AT_FDCWD, mnts[0]));

#if defined(FAN_MARK_ONLYDIR)
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_FLUSH | FAN_MARK_ONLYDIR,
		FAN_ACCESS, AT_FDCWD, mnts[0]));
#endif

#if defined(FAN_MARK_EVICTABLE)
	VOID_RET(int, fanotify_mark(ret_fd, FAN_MARK_EVICTABLE,
		FAN_ACCESS, AT_FDCWD, mnts[0]));
#endif

	(void)close(ret_fd);

	return 0;
}

/*
 *  fanotify_event_init()
 *	initialize fanotify
 */
static int fanotify_event_init(char *mounts[], const unsigned int flags)
{
	int fan_fd, count = 0, i;

	fan_fd = fanotify_init(flags, 0);
	if (fan_fd < 0)
		return -errno;

	/*
	 *  Gather all mounted file systems and monitor them
	 */
	for (i = 0; i < n_mnts; i++) {
		size_t j;
#if defined(FAN_MARK_MOUNT) || defined(FAN_MARK_FILESYSTEM)
		int ret;
#endif

		for (j = 0; j < SIZEOF_ARRAY(fan_stress_settings); j++) {
#if defined(FAN_MARK_MOUNT)
			ret = fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
				fan_stress_settings[j], AT_FDCWD, mounts[i]);
			if (ret == 0)
				count++;
#endif

#if defined(FAN_MARK_FILESYSTEM)
			ret = fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
				fan_stress_settings[j], AT_FDCWD, mounts[i]);
			if (ret == 0)
				count++;
#endif
		}
	}

	/* This really should not happen, / is always mounted */
	if (!count)
		return 0;
	return fan_fd;
}

/*
 *  fanotify_event_clear()
 *	exercise remove and flush fanotify_marking
 */
static void fanotify_event_clear(const int fan_fd)
{
#if defined(FAN_MARK_REMOVE)
	int i;

	/*
	 *  Gather all mounted file systems and monitor them
	 */
	for (i = 0; i < n_mnts; i++) {
		size_t j;

		for (j = 0; j < SIZEOF_ARRAY(fan_stress_settings); j++) {
#if defined(FAN_MARK_MOUNT)
			VOID_RET(int, fanotify_mark(fan_fd, FAN_MARK_REMOVE | FAN_MARK_MOUNT,
				fan_stress_settings[j], AT_FDCWD, mnts[i]));
#endif

#if defined(FAN_MARK_FILESYSTEM)
			VOID_RET(int, fanotify_mark(fan_fd, FAN_MARK_REMOVE | FAN_MARK_FILESYSTEM,
				fan_stress_settings[j], AT_FDCWD, mnts[i]));
#endif
		}
#if defined(FAN_MARK_FLUSH) &&	\
    defined(FAN_MARK_MOUNT)
		VOID_RET(int, fanotify_mark(fan_fd, FAN_MARK_FLUSH | FAN_MARK_MOUNT,
				0, AT_FDCWD, mnts[i]));
#endif
#if defined(FAN_MARK_FLUSH) &&	\
    defined(FAN_MARK_FILESYSTEM)
		VOID_RET(int, fanotify_mark(fan_fd, FAN_MARK_FLUSH | FAN_MARK_FILESYSTEM,
				0, AT_FDCWD, mnts[i]));
#endif
	}
#endif
}

/*
 *  stress_fanotify_init_exercise()
 *	exercise fanotify_init with specified flags
 */
static void stress_fanotify_init_exercise(const unsigned int flags)
{
	int ret_fd;

	ret_fd = fanotify_init(flags, 0);
	if (ret_fd != -1)
		(void)close(ret_fd);
}

static void stress_fanotify_read_events(
	stress_args_t *args,
	const int fan_fd,
	void *buffer,
	const size_t buffer_size,
	stress_fanotify_account_t *account)
{
	ssize_t len;
	struct fanotify_event_metadata *metadata;

	len = read(fan_fd, (void *)buffer, buffer_size);
	if (len <= 0)
		return;

	metadata = (struct fanotify_event_metadata *)buffer;

	while (FAN_EVENT_OK(metadata, len)) {
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if ((metadata->fd != FAN_NOFD) && (metadata->fd >= 0)) {
#if defined(FAN_OPEN)
			if (metadata->mask & FAN_OPEN)
				account->open++;
#endif
#if defined(FAN_CLOSE_WRITE)
			if (metadata->mask & FAN_CLOSE_WRITE)
				account->close_write++;
#endif
#if defined(FAN_CLOSE_NOWRITE)
			if (metadata->mask & FAN_CLOSE_NOWRITE)
				account->close_nowrite++;
#endif
#if defined(FAN_ACCESS)
			if (metadata->mask & FAN_ACCESS)
				account->access++;
#endif
#if defined(FAN_MODIFY)
			if (metadata->mask & FAN_MODIFY)
				account->modify++;
#endif
#if defined(FAN_RENAME)
			if (metadata->mask & FAN_RENAME)
				account->rename++;
#endif
			stress_bogo_inc(args);
			(void)close(metadata->fd);
		}
		metadata = FAN_EVENT_NEXT(metadata, len);
	}
}

/*
 *  stress_fanotify()
 *	stress fanotify
 */
static int stress_fanotify(stress_args_t *args)
{
	char pathname[PATH_MAX - 16], filename[PATH_MAX], filename2[PATH_MAX];
	pid_t pid;
	int ret, rc = EXIT_SUCCESS;
	stress_fanotify_account_t account;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)shim_memset(&account, 0, sizeof(account));

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	(void)stress_mk_filename(filename, sizeof(filename), pathname, "fanotify_file");
	(void)stress_mk_filename(filename2, sizeof(filename2), pathname, "fanotify_file2");
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pid = fork();

	/* do all mount points */
	(void)shim_memset(mnts, 0, sizeof(mnts));

	n_mnts = stress_mount_get(mnts, MAX_MNTS);
	if (n_mnts < 1) {
		pr_err("%s: cannot get mount point information\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	if (pid < 0) {
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	} else if (pid == 0) {
		/* Child */

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)sched_settings_apply(true);

		do {
			int fd;
			char buffer[64];

			/* Force FAN_CLOSE_NOWRITE */
			fd = creat(filename, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				pr_fail("%s: creat %s failed, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				_exit(EXIT_FAILURE);
			}
			(void)close(fd);

			/* Force FAN_CLOSE_WRITE */
			fd = open(filename, O_WRONLY, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				pr_fail("%s: open %s O_WRONLY failed, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				_exit(EXIT_FAILURE);
			}
			VOID_RET(ssize_t, write(fd, "test", 4));
			(void)close(fd);

			/* Force FAN_ACCESS */
			fd = open(filename, O_RDONLY, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				pr_fail("%s: open %s O_RDONLY failed, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				_exit(EXIT_FAILURE);
			}
			VOID_RET(ssize_t, read(fd, buffer, sizeof(buffer)));
			(void)close(fd);

			if (rename(filename, filename2) < 0)
				(void)shim_unlink(filename);
			else
				(void)shim_unlink(filename2);

		} while (stress_continue(args));

		_exit(EXIT_SUCCESS);
	} else {
		void *buffer;
		int fan_fd1;
#if defined(HAVE_SELECT)
		int max_fd;
#endif
#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_REPORT_DFID_NAME)
		int fan_fd2;
#endif
		double t, duration;

		fanotify_event_init_invalid();

		ret = posix_memalign(&buffer, BUFFER_SIZE, BUFFER_SIZE);
		if ((ret != 0) || (buffer == NULL)) {
			pr_err("%s: posix_memalign: cannot allocate 4K "
				"aligned buffer%s\n", args->name,
				stress_get_memfree_str());
			rc = EXIT_NO_RESOURCE;
			goto tidy;
		}

		fan_fd1 = fanotify_event_init(mnts, 0);
		if (fan_fd1 == 0) {
			pr_inf_skip("%s: no mount points found, "
				"skipping stressor\n", args->name);
			free(buffer);
			rc = EXIT_NO_RESOURCE;
			goto tidy;
		}
		if (fan_fd1 < 0) {
			switch (-fan_fd1) {
			case EMFILE:
				pr_inf_skip("%s: fanotify_init: too many open files, skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				break;
			case ENOMEM:
				pr_inf_skip("%s: fanotify_init: out of memory, skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				break;
			default:
				pr_fail("%s: fanotify_init failed, errno=%d (%s)\n",
					args->name, -fan_fd1, strerror(-fan_fd1));
				rc = EXIT_FAILURE;
			}
			free(buffer);
			goto tidy;
		}

#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_REPORT_DFID_NAME)
		fan_fd2 = fanotify_event_init(mnts, FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME);
		if (fan_fd2 < 0) {
			fan_fd2 = -1;
		}
#if defined(HAVE_SELECT)
		max_fd = STRESS_MAXIMUM(fan_fd1, fan_fd2);
#endif
#else
#if defined(HAVE_SELECT)
		max_fd = fan_fd1;
#endif
#endif
		ret = test_fanotify_mark(mnts);
		if (ret < 0) {
			free(buffer);
			switch (-ret) {
			case EMFILE:
				pr_inf_skip("%s: fanotify_init: too many open files, skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				break;
			case ENOMEM:
				pr_inf_skip("%s: fanotify_init: out of memory, skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				break;
			default:
				pr_fail("%s: fanotify_init failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			goto tidy;
		}

		t = stress_time_now();
		do {
			fd_set rfds;
			size_t i;

			FD_ZERO(&rfds);
			FD_SET(fan_fd1, &rfds);
#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_REPORT_DFID_NAME)
			if (fan_fd2 >= 0)
				FD_SET(fan_fd2, &rfds);
#endif
#if defined(HAVE_SELECT)
			ret = select(max_fd + 1, &rfds, NULL, NULL, NULL);
			if (ret == -1) {
				if (errno == EINTR)
					continue;
				pr_fail("%s: select failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				continue;
			}
			if (ret == 0)
				continue;
#endif

#if defined(FIONREAD)
			{
				int isz;

				/*
				 *  Force kernel to determine number
				 *  of bytes that are ready to be read
				 *  for some extra stress
				 */
				VOID_RET(int, ioctl(fan_fd1, FIONREAD, &isz));
			}
#endif
			if (FD_ISSET(fan_fd1, &rfds))
				stress_fanotify_read_events(args, fan_fd1, buffer, BUFFER_SIZE, &account);
#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_REPORT_DFID_NAME)
			if ((fan_fd2 >= 0) && FD_ISSET(fan_fd2, &rfds))
				stress_fanotify_read_events(args, fan_fd2, buffer, BUFFER_SIZE, &account);
#endif

			/*
			 * Exercise fanotify_init with all possible values
			 * of flag argument to increase kernel coverage
			 */
			for (i = 0; i < SIZEOF_ARRAY(init_flags); i++) {
				stress_fanotify_init_exercise(init_flags[i]);
			}
		} while (stress_continue(args));

		duration = stress_time_now() - t;

		free(buffer);
		fanotify_event_clear(fan_fd1);
		(void)close(fan_fd1);
#if defined(FAN_CLASS_NOTIF) &&		\
    defined(FAN_REPORT_DFID_NAME)
		if (fan_fd2 >= 0) {
			fanotify_event_clear(fan_fd2);
			(void)close(fan_fd2);
		}
#endif
		if (duration > 0.0) {
			stress_metrics_set(args, 0, "opens/sec",
				(double)account.open / duration, STRESS_METRIC_GEOMETRIC_MEAN);
			stress_metrics_set(args, 1, "close writes/sec",
				(double)account.close_write / duration, STRESS_METRIC_GEOMETRIC_MEAN);
			stress_metrics_set(args, 2, "close no-writes/sec",
				(double)account.close_nowrite / duration, STRESS_METRIC_GEOMETRIC_MEAN);
			stress_metrics_set(args, 3, "accesses/sec",
				(double)account.access / duration, STRESS_METRIC_GEOMETRIC_MEAN);
			stress_metrics_set(args, 4, "modifies/sec",
				(double)account.modify / duration, STRESS_METRIC_GEOMETRIC_MEAN);
			/* stress_metrics_set(args, 5, "renames/sec",
				(double)account.rename / duration, STRESS_METRIC_GEOMETRIC_MEAN); */
		}
	}
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (pid > 0)
		(void)stress_kill_pid_wait(pid, NULL);
	(void)shim_unlink(filename);
	(void)shim_unlink(filename2);
	shim_sync();
	(void)stress_temp_dir_rm_args(args);
	stress_mount_free(mnts, n_mnts);

	return rc;
}

const stressor_info_t stress_fanotify_info = {
	.stressor = stress_fanotify,
	.supported = stress_fanotify_supported,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_fanotify_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/fanotify.h"
};
#endif
