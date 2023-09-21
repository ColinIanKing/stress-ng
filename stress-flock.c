// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"flock N",	"start N workers locking a single file" },
	{ NULL,	"flock-ops N",	"stop after N flock bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)

#define MAX_FLOCK_STRESSORS	(3)

static int stress_flock_child(
	const stress_args_t *args,
	const char *filename,
	const int bad_fd,
	const bool save_metrics)
{
	bool cont;
	int i, rc = EXIT_SUCCESS;
	int fd1, fd2;
	double lock_duration = 0.0, lock_count = 0.0;
	double unlock_duration = 0.0, unlock_count = 0.0;
	double rate;

	fd1 = open(filename, O_RDONLY);
	if (fd1 < 0) {
		pr_err("%s: failed to open %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	fd2 = open(filename, O_RDONLY);
	if (fd2 < 0) {
		pr_err("%s: failed to open %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)close(fd1);
		return EXIT_FAILURE;
	}

	for (i = 0; ; i++) {
		double t;

#if defined(LOCK_EX)
		t = stress_time_now();
		if (flock(fd1, LOCK_EX) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			/*
			 *  we have an exclusive lock on the fd, so re-doing
			 *  the exclusive lock with LOCK_NB should not succeed
			 */
			if (flock(fd2, LOCK_EX | LOCK_NB) == 0) {
				pr_fail("%s: unexpectedly able to double lock file using LOCK_EX, expecting error EAGAIN\n",
					args->name);
				rc = EXIT_FAILURE;
				(void)flock(fd2, LOCK_UN);
				break;
			}

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif

		/*
		 *  Exercise flock with invalid fd
		 */
		(void)flock(bad_fd, LOCK_EX);
		(void)flock(bad_fd, LOCK_UN);

#if defined(LOCK_NB)
		t = stress_time_now();
		if (flock(fd1, LOCK_EX | LOCK_NB) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}

		/*
		 *  Exercise flock with invalid operation
		 */
		{
			int ret;

			ret = flock(fd1, LOCK_NB);
			if (ret == 0) {
				pr_fail("%s: flock failed expected EINVAL, instead got "
					"errno=%d (%s)\n", args->name, errno, strerror(errno));
				(void)flock(fd1, LOCK_UN);
			}
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_SH)
		if (!stress_continue(args))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_SH) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_SH) &&		\
    defined(LOCK_NB)
		if (!stress_continue(args))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_SH | LOCK_NB) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_MAND) &&	\
    defined(LOCK_READ)
		if (!stress_continue(args))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_MAND | LOCK_READ) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_MAND) &&	\
    defined(LOCK_WRITE)
		if (!stress_continue(args))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_MAND | LOCK_WRITE) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_EX) &&		\
    defined(LOCK_SH)
		if (!stress_continue(args))
			break;

		/* Exercise invalid lock combination */
		t = stress_time_now();
		if (flock(fd1, LOCK_EX | LOCK_SH) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (cont)
				stress_bogo_inc(args);

			t = stress_time_now();
			if (flock(fd1, LOCK_UN) == 0) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (!cont)
				break;
		}
#else
		UNEXPECTED
#endif
#if defined(__linux__)
		if ((i & 0xff) == 0) {
			char buf[4096];

			VOID_RET(ssize_t, stress_system_read("/proc/locks", buf, sizeof(buf)));
		}
#endif
	}
	if (save_metrics) {
		rate = (lock_count > 0.0) ? lock_duration / lock_count : 0.0;
		stress_metrics_set(args, 0, "nanosecs per flock lock call", rate * STRESS_DBL_NANOSECOND);
		rate = (unlock_count > 0.0) ? unlock_duration / unlock_count : 0.0;
		stress_metrics_set(args, 1, "nanosecs per flock unlock call", rate * STRESS_DBL_NANOSECOND);
	}
	(void)close(fd2);
	(void)close(fd1);

	return rc;
}

/*
 *  stress_flock
 *	stress file locking
 */
static int stress_flock(const stress_args_t *args)
{
	int fd, ret, rc = EXIT_FAILURE;
	const int bad_fd = stress_get_bad_fd();
	size_t i;
	pid_t pids[MAX_FLOCK_STRESSORS];
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_err("%s: failed to create %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err;
	}
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(pids, 0, sizeof(pids));
	for (i = 0; i < MAX_FLOCK_STRESSORS; i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			goto reap;
		} else if (pids[i] == 0) {
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			_exit(stress_flock_child(args, filename, bad_fd, false));
		}
	}

	stress_flock_child(args, filename, bad_fd, true);
	rc = EXIT_SUCCESS;
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, pids, MAX_FLOCK_STRESSORS, SIGALRM, true);
	(void)shim_unlink(filename);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_flock_info = {
	.stressor = stress_flock,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_flock_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without flock() or LOCK_EX/LOCK_UN support"
};
#endif
