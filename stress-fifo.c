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
#include "core-builtin.h"
#include "core-killpid.h"

#include <sys/ioctl.h>

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#define MIN_FIFO_READERS	(1)
#define MAX_FIFO_READERS	(64)
#define DEFAULT_FIFO_READERS	(4)

#define MIN_FIFO_DATA_SIZE	(sizeof(uint64_t))
#define MAX_FIFO_DATA_SIZE	(4096)
#define DEFAULT_FIFO_DATA_SIZE	(MIN_FIFO_DATA_SIZE)

static const stress_help_t help[] = {
	{ NULL,	"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,	"fifo-data-size N",	"set fifo read/write size in bytes (default 8)" },
	{ NULL,	"fifo-ops N",		"stop after N fifo bogo operations" },
	{ NULL,	"fifo-readers N",	"number of fifo reader stressors to start" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_fifo_data_size, "fifo-data-size", TYPE_ID_SIZE_T_BYTES_VM, MIN_FIFO_DATA_SIZE, MAX_FIFO_DATA_SIZE, NULL },
	{ OPT_fifo_readers,   "fifo-readers",   TYPE_ID_UINT64, MIN_FIFO_READERS, MAX_FIFO_READERS, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_MKFIFO)

static const uint64_t wrap_mask = 0xffff000000000000ULL;

/*
 *  fifo_spawn()
 *	spawn a process
 */
static pid_t fifo_spawn(
	stress_args_t *args,
	void (*func)(stress_args_t *args, const char *name,
		     const char *fifoname, const size_t fifo_data_size),
	const char *name,
	const char *fifoname,
	const size_t fifo_data_size,
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid)
{
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		return -1;
	} else if (s_pid->pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
		s_pid->pid = getpid();
		stress_sync_start_wait_s_pid(s_pid);
		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		func(args, name, fifoname, fifo_data_size);
		stress_set_proc_state(args->name, STRESS_STATE_WAIT);
		_exit(EXIT_SUCCESS);
	} else {
		stress_sync_start_s_pid_list_add(s_pids_head, s_pid);
	}
	return s_pid->pid;
}

/*
 *  stress_fifo_readers()
 *	read fifo
 */
static void stress_fifo_reader(
	stress_args_t *args,
	const char *name,
	const char *fifoname,
	const size_t fifo_data_size)
{
	int fd, count = 0;
	uint64_t lastval = 0;
	uint64_t ALIGN64 buf[MAX_FIFO_DATA_SIZE / sizeof(uint64_t)];

	fd = open(fifoname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_fail("%s: fifo read open failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return;
	}
	while (stress_continue_flag()) {
		ssize_t sz;
#if defined(HAVE_POLL_H) && 	\
    defined(HAVE_POLL)
		int ret;
		struct pollfd fds;

		fds.fd = fd;
		fds.events = POLLIN;
redo_poll:
		ret = poll(&fds, 1, 1000);
		if (UNLIKELY(ret < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err("%s: poll failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		} else if (ret == 0) {
			/*
			 * nothing to read, timed-out, retry
			 * as this can happen on a highly
			 * overloaded stressed system
			 */
			if (LIKELY(stress_continue(args)))
				goto redo_poll;
			break;
		}
#elif defined(HAVE_SELECT)
		int ret;
		struct timeval timeout;
		fd_set rdfds;

		FD_ZERO(&rdfds);
		FD_SET(fd, &rdfds);
redo_select:
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		ret = select(fd + 1, &rdfds, NULL, NULL, &timeout);
		if (UNLIKELY(ret < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err("%s: select failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		} else if (ret == 0) {
			/*
			 * nothing to read, timed-out, retry
			 * as this can happen on a highly
			 * overloaded stressed system
			 */
			if (LIKELY(stress_continue(args)))
				goto redo_select;
			break;
		}
#endif
#if defined(FIONREAD)
		if ((count & 0xff) == 0) {
			int isz = 0;

			VOID_RET(int, ioctl(fd, FIONREAD, &isz));
		}
#else
		UNEXPECTED
#endif
		sz = read(fd, buf, fifo_data_size);
		if (UNLIKELY(sz < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail("%s: fifo read failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		if (UNLIKELY(sz == 0))
			break;
		if (UNLIKELY(sz != (ssize_t)fifo_data_size)) {
			pr_fail("%s: fifo read did not get buffer of size %zu\n",
				name, fifo_data_size);
			break;
		}
		if (UNLIKELY(((buf[0] < lastval) &&
			     ((~buf[0] & wrap_mask) && (lastval & wrap_mask))))) {
			pr_fail("%s: fifo read did not get "
				"expected value\n", name);
			break;
		}
		lastval = buf[0];

		if (UNLIKELY((count & 0x1ff) == 0)) {
			void *ptr;

			/* Exercise lseek -> ESPIPE */
			VOID_RET(off_t, lseek(fd, 0, SEEK_CUR));

			/* Exercise mmap -> ENODEV */
			ptr = mmap(NULL, args->page_size, PROT_READ,
				MAP_PRIVATE, fd, 0);
			if (ptr != MAP_FAILED)
				(void)munmap(ptr, args->page_size);
		}
		count++;
	}
	(void)close(fd);
}

/*
 *  stress_fifo
 *	stress by heavy fifo I/O
 */
static int stress_fifo(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	int fd;
	char fifoname[PATH_MAX];
	uint64_t i;
	uint64_t fifo_readers = DEFAULT_FIFO_READERS;
	int rc = EXIT_SUCCESS;
	double t, fifo_duration = 0.0, fifo_count = 0.0, rate;
	size_t fifo_data_size = DEFAULT_FIFO_DATA_SIZE;
	uint64_t ALIGN64 buf[MAX_FIFO_DATA_SIZE / sizeof(uint64_t)];
	char msg[64];

	if (!stress_get_setting("fifo-readers", &fifo_readers)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fifo_readers = MAX_FIFO_READERS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fifo_readers = MIN_FIFO_READERS;
	}

	if (!stress_get_setting("fifo-data-size", &fifo_data_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fifo_data_size = MAX_FIFO_DATA_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fifo_data_size = MIN_FIFO_DATA_SIZE;
	}

	s_pids = stress_sync_s_pids_mmap(MAX_FIFO_READERS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, MAX_FIFO_READERS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		rc = stress_exit_status(-rc);
		goto tidy_pids;
	}

	(void)stress_temp_filename_args(args,
		fifoname, sizeof(fifoname), stress_mwc32());

	if (mkfifo(fifoname, S_IRUSR | S_IWUSR) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: mkfifo failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}

#if defined(HAVE_PATHCONF) &&	\
    defined(_PC_PIPE_BUF)
	VOID_RET(long int, pathconf(fifoname, _PC_PIPE_BUF));
#endif

	for (i = 0; i < fifo_readers; i++) {
		stress_sync_start_init(&s_pids[i]);

		if (UNLIKELY(fifo_spawn(args, stress_fifo_reader, args->name, fifoname, fifo_data_size, &s_pids_head, &s_pids[i]) < 0)) {
			rc = EXIT_NO_RESOURCE;
			goto reap;
		}
		if (UNLIKELY(!stress_continue_flag())) {
			rc = EXIT_SUCCESS;
			goto reap;
		}
	}

	(void)shim_memset(buf, 0xaa, sizeof(buf));
	buf[0] = 0ULL;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	fd = open(fifoname, O_WRONLY);
	if (fd < 0) {
		if (errno == EINTR) {
			rc = 0;
		} else {
			rc = stress_exit_status(fd);
			pr_fail("%s: fifo write open failed, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		goto reap;
	}

	t = stress_time_now();
	do {
		ssize_t ret;

		ret = write(fd, buf, fifo_data_size);
		if (ret > 0) {
			fifo_count += 1.0;
		} else {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				break;
			}
			continue;
		}
		buf[0]++;
		buf[0] &= ~wrap_mask;
		stress_bogo_inc(args);
	} while (stress_continue(args));
	fifo_duration = stress_time_now() - t;

	rate = (fifo_duration > 0.0) ? (fifo_count / fifo_duration) : 0.0;
	(void)snprintf(msg, sizeof(msg), "fifo %zu byte writes per sec",
		fifo_data_size);
	stress_metrics_set(args, 0, msg, rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_kill_and_wait_many(args, s_pids, fifo_readers, SIGALRM, false);

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_unlink(fifoname);
	(void)stress_temp_dir_rm_args(args);
tidy_pids:
	(void)stress_sync_s_pids_munmap(s_pids, MAX_FIFO_READERS);

	return rc;
}

const stressor_info_t stress_fifo_info = {
	.stressor = stress_fifo,
	.classifier = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_fifo_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/select.h"
};
#endif
