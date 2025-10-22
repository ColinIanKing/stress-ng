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

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

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
	stress_args_t *args,
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
		pr_err("%s: failed to open %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	fd2 = open(filename, O_RDONLY);
	if (fd2 < 0) {
		pr_err("%s: failed to open %s, errno=%d (%s)\n",
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
			if (LIKELY(cont))
				stress_bogo_inc(args);

			/*
			 *  we have an exclusive lock on the fd, so re-doing
			 *  the exclusive lock with LOCK_NB should not succeed
			 */
			if (UNLIKELY(flock(fd2, LOCK_EX | LOCK_NB) == 0)) {
				pr_fail("%s: unexpectedly able to double lock file using LOCK_EX, expecting error EAGAIN\n",
					args->name);
				rc = EXIT_FAILURE;
				(void)flock(fd2, LOCK_UN);
				break;
			}

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
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
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}

		/*
		 *  Exercise flock with invalid operation
		 */
		{
			int ret;

			ret = flock(fd1, LOCK_NB);
			if (UNLIKELY(ret == 0)) {
				pr_fail("%s: flock failed expected EINVAL, instead got "
					"errno=%d (%s)\n", args->name, errno, strerror(errno));
				(void)flock(fd1, LOCK_UN);
			}
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_SH)
		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_SH) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_SH) &&		\
    defined(LOCK_NB)
		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_SH | LOCK_NB) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_MAND) &&	\
    defined(LOCK_READ)
		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_MAND | LOCK_READ) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_MAND) &&	\
    defined(LOCK_WRITE)
		if (UNLIKELY(!stress_continue(args)))
			break;

		t = stress_time_now();
		if (flock(fd1, LOCK_MAND | LOCK_WRITE) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(LOCK_EX) &&		\
    defined(LOCK_SH)
		if (UNLIKELY(!stress_continue(args)))
			break;

		/* Exercise invalid lock combination */
		t = stress_time_now();
		if (flock(fd1, LOCK_EX | LOCK_SH) == 0) {
			lock_duration += stress_time_now() - t;
			lock_count += 1.0;

			cont = stress_continue(args);
			if (LIKELY(cont))
				stress_bogo_inc(args);

			t = stress_time_now();
			if (LIKELY(flock(fd1, LOCK_UN) == 0)) {
				unlock_duration += stress_time_now() - t;
				unlock_count += 1.0;
			}
			if (UNLIKELY(!cont))
				break;
		}
#else
		UNEXPECTED
#endif
#if defined(__linux__)
		if (UNLIKELY((i & 0xff) == 0))
			(void)stress_system_discard("/proc/locks");
#else
		(void)i;
#endif
	}
	if (save_metrics) {
		rate = (lock_count > 0.0) ? lock_duration / lock_count : 0.0;
		stress_metrics_set(args, 0, "nanosecs per flock lock call",
			rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
		rate = (unlock_count > 0.0) ? unlock_duration / unlock_count : 0.0;
		stress_metrics_set(args, 1, "nanosecs per flock unlock call",
			rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	}
	(void)close(fd2);
	(void)close(fd1);

	return rc;
}

/*
 *  stress_flock
 *	stress file locking
 */
static int stress_flock(stress_args_t *args)
{
	int fd, ret, rc = EXIT_FAILURE;
	const int bad_fd = stress_get_bad_fd();
	size_t i;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	char filename[PATH_MAX];

	s_pids = stress_sync_s_pids_mmap(MAX_FLOCK_STRESSORS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, MAX_FLOCK_STRESSORS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
        }

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto err_free_s_pids;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_err("%s: failed to create %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err;
	}
	(void)close(fd);

	for (i = 0; i < MAX_FLOCK_STRESSORS; i++) {
		stress_sync_start_init(&s_pids[i]);

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			goto reap;
		} else if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			_exit(stress_flock_child(args, filename, bad_fd, false));
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_flock_child(args, filename, bad_fd, true);
	rc = EXIT_SUCCESS;
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, s_pids, MAX_FLOCK_STRESSORS, SIGALRM, true);
	(void)shim_unlink(filename);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
err_free_s_pids:
	(void)stress_sync_s_pids_munmap(s_pids, MAX_FLOCK_STRESSORS);

	return rc;
}

const stressor_info_t stress_flock_info = {
	.stressor = stress_flock,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_flock_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without flock() or LOCK_EX/LOCK_UN support"
};
#endif
