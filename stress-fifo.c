// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#define MIN_FIFO_READERS	(1)
#define MAX_FIFO_READERS	(64)
#define DEFAULT_FIFO_READERS	(4)

#define MIN_FIFO_DATA_SIZE	(sizeof(uint64_t))
#define MAX_FIFO_DATA_SIZE	(4096)
#define DEFAULT_FIFO_DATA_SIZE	(MIN_FIFO_DATA_SIZE)

#if defined(HAVE_SYS_SELECT_H)
static const uint64_t wrap_mask = 0xffff000000000000ULL;
#endif

static const stress_help_t help[] = {
	{ NULL,	"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,	"fifo-data-size N",	"set fifo read/write size in bytes (default 8)" },
	{ NULL,	"fifo-ops N",		"stop after N fifo bogo operations" },
	{ NULL,	"fifo-readers N",	"number of fifo reader stressors to start" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_fifo_readers(const char *opt)
{
	uint64_t fifo_readers;

	fifo_readers = stress_get_uint64(opt);
	stress_check_range("fifo-readers", fifo_readers,
		MIN_FIFO_READERS, MAX_FIFO_READERS);
	return stress_set_setting("fifo-readers", TYPE_ID_UINT64, &fifo_readers);
}

/*
 *  stress_set_fifo_data_size()
 *	set fifo data read/write size in bytes
 */
static int stress_set_fifo_data_size(const char *opt)
{
	size_t fifo_data_size;

	fifo_data_size = (size_t)stress_get_uint64_byte(opt);
	stress_check_range_bytes("fifo-data-size", fifo_data_size,
		MIN_FIFO_DATA_SIZE, MAX_FIFO_DATA_SIZE);
	return stress_set_setting("fifo-data-size", TYPE_ID_SIZE_T, &fifo_data_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_fifo_data_size,	stress_set_fifo_data_size },
	{ OPT_fifo_readers,	stress_set_fifo_readers },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_SELECT_H)
/*
 *  fifo_spawn()
 *	spawn a process
 */
static pid_t fifo_spawn(
	const stress_args_t *args,
	void (*func)(const stress_args_t *args, const char *name,
		     const char *fifoname, const size_t fifo_data_size),
	const char *name,
	const char *fifoname,
	const size_t fifo_data_size)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		func(args, name, fifoname, fifo_data_size);
		stress_set_proc_state(args->name, STRESS_STATE_WAIT);
		_exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  stress_fifo_readers()
 *	read fifo
 */
static void stress_fifo_reader(
	const stress_args_t *args,
	const char *name,
	const char *fifoname,
	const size_t fifo_data_size)
{
	int fd, count = 0;
	uint64_t lastval = 0;
	uint64_t ALIGN64 buf[MAX_FIFO_DATA_SIZE / sizeof(uint64_t)];

	fd = open(fifoname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_fail("%s: fifo read open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return;
	}
	while (stress_continue_flag()) {
		ssize_t sz;
#if defined(HAVE_SELECT)
		int ret;
		struct timeval timeout;
		fd_set rdfds;

		FD_ZERO(&rdfds);
		FD_SET(fd, &rdfds);
redo:
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		ret = select(fd + 1, &rdfds, NULL, NULL, &timeout);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err("%s: select failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		} else if (ret == 0) {
			/*
			 * nothing to read, timed-out, retry
			 * as this can happen on a highly
			 * overloaded stressed system
			 */
			if (stress_continue(args))
				goto redo;
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
		if (sz < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail("%s: fifo read failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		if (sz == 0)
			break;
		if (sz != (ssize_t)fifo_data_size) {
			pr_fail("%s: fifo read did not get buffer of size %zu\n",
				name, fifo_data_size);
			break;
		}
		if ((buf[0] < lastval) &&
		    ((~buf[0] & wrap_mask) && (lastval & wrap_mask))) {
			pr_fail("%s: fifo read did not get "
				"expected value\n", name);
			break;
		}
		lastval = buf[0];

		if ((count & 0x1ff) == 0) {
			void *ptr;

			/* Exercise lseek -> ESPIPE */
			VOID_RET(off_t, lseek(fd, 0, SEEK_CUR));

			/* Exercise mmap -> ENODEV */
			ptr = mmap(NULL, args->page_size, PROT_READ,
				MAP_PRIVATE, fd, 0);
			if (ptr)
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
static int stress_fifo(const stress_args_t *args)
{
	pid_t pids[MAX_FIFO_READERS];
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

	(void)stress_get_setting("fifo-data-size", &fifo_data_size);

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return stress_exit_status(-rc);

	(void)stress_temp_filename_args(args,
		fifoname, sizeof(fifoname), stress_mwc32());

	if (mkfifo(fifoname, S_IRUSR | S_IWUSR) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: mkfifo failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}

	(void)shim_memset(pids, 0, sizeof(pids));
	for (i = 0; i < fifo_readers; i++) {
		pids[i] = fifo_spawn(args, stress_fifo_reader, args->name, fifoname, fifo_data_size);
		if (pids[i] < 0) {
			rc = EXIT_NO_RESOURCE;
			goto reap;
		}
		if (!stress_continue_flag()) {
			rc = EXIT_SUCCESS;
			goto reap;
		}
	}

	fd = open(fifoname, O_WRONLY);
	if (fd < 0) {
		if (errno == EINTR) {
			rc = 0;
		} else {
			rc = stress_exit_status(fd);
			pr_fail("%s: fifo write open failed: "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		goto reap;
	}

	(void)shim_memset(buf, 0xaa, sizeof(buf));
	buf[0] = 0ULL;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
	(void)snprintf(msg, sizeof(msg), "fifo %zu byte writes per sec", fifo_data_size);
	stress_metrics_set(args, 0, msg, rate);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_kill_and_wait_many(args, pids, fifo_readers, SIGALRM, false);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_unlink(fifoname);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_fifo_info = {
	.stressor = stress_fifo,
	.class = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_fifo_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/select.h"
};
#endif
