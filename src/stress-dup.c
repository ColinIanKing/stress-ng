/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"dup N",	"start N workers exercising dup/close" },
	{ NULL,	"dup-ops N",	"stop after N dup/close bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    defined(HAVE_CLONE) &&	\
    defined(CLONE_VM) &&	\
    defined(CLONE_FILES)
#define STRESS_DUP2_RACE	(1)
#endif

#if defined(STRESS_DUP2_RACE)

#define DUP_STACK_SIZE	(16364 / sizeof(uint64_t))

/*
 *  dup2 race context
 */
typedef struct {
	int	fd;			/* temp file descriptor */
	int	fd_pipe;		/* fifo file descriptor */
	uint64_t race_count;		/* count of dup2 races */
	uint64_t try_count;		/* dup2 race attempts */
	char 	fifoname[PATH_MAX];	/* name of fifo file */
	pid_t	pid_clone;		/* pid of clone process */
	uint64_t stack[DUP_STACK_SIZE];	/* clone stack */
} info_t;

static int stress_dup2_race_clone(void *arg)
{
	int fd, fd_dup;
	info_t *info = (info_t *)arg;

	/* Should never be null, but weird things may happen  */
	if (!info)
		_exit(1);

	if ((fd = open("/dev/null", O_RDONLY)) == -1)
		_exit(1);

	/*
	 *  Unexpected, the next fd is the same as an earlier
	 *  previous fd, so don't dup2 on this
	 */
	if (fd == info->fd)
		_exit(0);

	/*
	 *  Race with the fd on the fifo open. The fifo
	 *  is in a partially opened state and this dup2
	 *  call will use the same fd and if there is a
	 *  fd race collisiom we get EBUSY on linux.
	 */
	fd_dup = dup2(fd, info->fd);
	if ((fd_dup < 0) && (errno == EBUSY))
		info->race_count++;
	(void)close(fd);

	_exit(0);
}

static void stress_dup2_sigalrm(int sig)
{
	(void)sig;
}

static int static_dup2_child(info_t *info)
{
	struct sigaction action;
	struct itimerval timer;
	pid_t child_tid = -1, parent_tid = -1;
	const ssize_t stack_offset =
                stress_get_stack_direction() *
                (sizeof(info->stack)- 64);
	char *stack_top = ((char *)info->stack) + stack_offset;

	info->fd_pipe = -1;
	info->pid_clone = -1;

	(void)memset(&action, 0, sizeof(action));
	action.sa_flags = 0;
	action.sa_handler = stress_dup2_sigalrm;
	if (sigaction(SIGALRM, &action, NULL) < 0)
		_exit(1);

	/*
	 *  Find next free fd, we're going to race on this
	 *  fd number with a process that shares the same fd table
	 */
	info->fd = open("/dev/null", O_RDONLY);
	if (info->fd < 0)
		_exit(1);
	(void)close(info->fd);

	info->pid_clone = clone(stress_dup2_race_clone,
		stress_align_stack(stack_top),
		CLONE_VM | CLONE_FILES | SIGCHLD, info, &parent_tid,
		NULL, &child_tid);
	if (info->pid_clone < 0)
		_exit(1);

	/*
	 *  Set a timer to unblock pipe read after 1ms
	 */
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 1000;
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 1000;
	if (setitimer(ITIMER_REAL, &timer, NULL) < 0)
		_exit(1);

	info->try_count++;
	/*
	 *  Open will block until we get a sigalarm on Linux,
	 *  can't guarantee this on other kernels. This will
	 *  block and cause a race -EBUSY return with the
	 *  clone dup2 open process
	 */
	info->fd_pipe = open(info->fifoname, O_RDONLY);

	/*
	 *  Cancel timer
	 */
	(void)memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);

	/*
	 *  Unlikely to be open, but close it to be a good citizen
	 */
	if (info->fd_pipe >= 0)
		(void)close(info->fd_pipe);

	/*
	 *  Should always be true..
	 */
	if (info->pid_clone >= 0) {
		int status;

		kill(info->pid_clone, SIGKILL);
		waitpid(info->pid_clone, &status, __WCLONE);
	}

	(void)close(info->fd);

	return 0;
}

/*
 *  Run the dup2 parent/clone in a new process context
 *  to avoid any potential breaking of the parent fd
 *  table (avoid any weird issues).
 */
static int stress_dup2_race(info_t *info)
{
	pid_t pid;
	if (mkfifo(info->fifoname, S_IRUSR | S_IWUSR))
		return -1;

	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		static_dup2_child(info);
		_exit(0);
	} else {
		int status;

		(void)waitpid(pid, &status, 0);
	}
	(void)unlink(info->fifoname);

	return 0;
}
#endif

/*
 *  stress_dup()
 *	stress system by rapid dup/close calls
 */
static int stress_dup(const stress_args_t *args)
{
	static int fds[STRESS_FD_MAX];
	size_t max_fd = stress_get_file_limit();
	size_t i;
	bool do_dup3 = true;
	const int bad_fd = stress_get_bad_fd();
#if defined(STRESS_DUP2_RACE)
	info_t *info;

	info = mmap(NULL, sizeof(*info), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (info != MAP_FAILED) {
		if (stress_temp_dir_mk(args->name, args->pid, args->instance) < 0)
			goto tidy_dir;

		(void)stress_temp_filename_args(args, info->fifoname,
			sizeof(info->fifoname), stress_mwc32());
	}
#endif

	if (max_fd > SIZEOF_ARRAY(fds))
		max_fd = SIZEOF_ARRAY(fds);

	fds[0] = open("/dev/zero", O_RDONLY);
	if (fds[0] < 0) {
		pr_dbg("%s: open failed on /dev/zero, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t n;

		for (n = 1; n < max_fd; n++) {
			int tmp;

			fds[n] = dup(fds[0]);
			if (fds[n] < 0)
				break;

			/* do an invalid dup on an invalid fd */
			tmp = dup(bad_fd);
			if (tmp >= 0)
				(void)close(tmp);

			if (!keep_stressing(args))
				break;

			/* do an invalid dup3 on an invalid fd */
			tmp = shim_dup3(fds[0], bad_fd, O_CLOEXEC);
			if (tmp >= 0)
				(void)close(tmp);
			else if (errno == ENOSYS)
				do_dup3 = false;

			if (!keep_stressing(args))
				break;

			/* do an invalid dup3 with an invalid flag */
			tmp = shim_dup3(fds[0], fds[n], INT_MIN);
			if (tmp >= 0)
				(void)close(tmp);
			else if (errno == ENOSYS)
				do_dup3 = false;

			if (!keep_stressing(args))
				break;

			/* do an invalid dup3 with an invalid fd */
			tmp = shim_dup3(bad_fd, fds[n], INT_MIN);
			if (tmp >= 0)
				(void)close(tmp);
			else if (errno == ENOSYS)
				do_dup3 = false;

			if (!keep_stressing(args))
				break;

			/* do an invalid dup3 with same oldfd and newfd */
			tmp = shim_dup3(fds[0], fds[0], O_CLOEXEC);
			if (tmp >= 0)
				(void)close(tmp);
			else if (errno == ENOSYS)
				do_dup3 = false;

			if (!keep_stressing(args))
				break;

			if (do_dup3 && stress_mwc1()) {
				int fd;

				fd = shim_dup3(fds[0], fds[n], O_CLOEXEC);
				/* No dup3 support? then fallback to dup2 */
				if ((fd < 0) && (errno == ENOSYS)) {
					fd = dup2(fds[0], fds[n]);
					do_dup3 = false;
				}
				fds[n] = fd;
			} else {
				fds[n] = dup2(fds[0], fds[n]);
			}

			if (!keep_stressing(args))
				break;

			fds[n] = dup2(fds[0], fds[n]);
			if (fds[n] < 0)
				break;

			if (!keep_stressing(args))
				break;

			/* dup2 on the same fd should be a no-op */
			tmp = dup2(fds[n], fds[n]);
			if (tmp != fds[n]) {
				pr_fail("%s: dup2 failed with same fds, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			/* do an invalid dup2 on an invalid fd */
			tmp = dup2(fds[0], bad_fd);
			if (tmp >= 0)
				(void)close(tmp);

			if (!keep_stressing(args))
				break;

#if defined(F_DUPFD)
			/* POSIX.1-2001 fcntl() */

			(void)close(fds[n]);
			fds[n] = fcntl(fds[0], F_DUPFD, fds[0]);
			if (fds[n] < 0)
				break;

			if (!keep_stressing(args))
				break;
#endif

#if defined(STRESS_DUP2_RACE)
			if (info != MAP_FAILED)
				stress_dup2_race(info);
#endif

			inc_counter(args);
		}
		for (i = 1; i < n; i++) {
			if (fds[i] < 0)
				break;
			if (!keep_stressing_flag())
				break;
			(void)close(fds[i]);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds[0]);

#if defined(STRESS_DUP2_RACE)
tidy_dir:
	if (info != MAP_FAILED) {
		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
		if (info->fifoname[0])
			(void)unlink(info->fifoname);
		(void)stress_temp_dir_rm_args(args);
		pr_dbg("%s: dup2: %" PRIu64 " races from %" PRIu64 " attempts (%.2f%%)\n",
			args->name, info->race_count, info->try_count,
			info->try_count > 0 ?
				(float)info->race_count / (float)info->try_count * 100.0 : 0.0);
		(void)munmap(info, sizeof(*info));
	}
#endif

	return EXIT_SUCCESS;
}

stressor_info_t stress_dup_info = {
	.stressor = stress_dup,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
