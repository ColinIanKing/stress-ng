/*
 * Copyright (C) 2024      Colin Ian King.
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

#define UNLINK_PROCS		(3)
#define UNLINK_FILES		(1024)	/* must be power of 2 and less than 65536 */

static const stress_help_t help[] = {
	{ NULL,	"unlink N",		"start N unlink exercising stressors" },
	{ NULL,	"unlink-ops N",		"stop after N unlink exercising bogo operations" },
	{ NULL,	NULL,			NULL }
};

static void stress_unlink_shuffle(size_t idx[UNLINK_FILES], const size_t mask)
{
	register size_t i;

	for (i = 0; i < UNLINK_FILES; i++) {
		register const size_t j = stress_mwc16() & mask;
		register const size_t tmp = idx[i];

		idx[i] = idx[j];
		idx[j] = tmp;
	}
}

/*
 *  stress_unlink_exercise()
 *	create files, unlink and close them in randomized order
 */
static void stress_unlink_exercise(
	stress_args_t *args,
	const bool parent,
	stress_metrics_t *metrics,
	char *filenames[UNLINK_FILES])
{
	int fds[UNLINK_FILES], n;
	size_t idx[UNLINK_FILES];
	register size_t i;
	const size_t mask = UNLINK_FILES - 1;
	double t;

	/* Various open mode flags to be selected randomly */
	static const int open_flags[] = {
#if defined(O_EXCL)
		O_EXCL,
#endif
#if defined(O_DIRECT)
		O_DIRECT,
#endif
#if defined(O_DSYNC)
		O_DSYNC,
#endif
#if defined(O_NOATIME)
		O_NOATIME,
#endif
#if defined(O_SYNC)
		O_SYNC,
#endif
#if defined(O_TRUNC)
		O_TRUNC,
#endif
		0,
	};

	stress_mwc_reseed();

	for (i = 0; i < UNLINK_FILES; i++)
		idx[i] = i;

	stress_unlink_shuffle(idx, mask);

	do {
		for (i = 0; stress_continue(args) && (i < UNLINK_FILES); i++) {
			int mode, retries = 0;

			fds[i] = -1;

			if ((i & 7) == 7) {
				if (link(filenames[i - 1], filenames[i]) == 0) {
					fds[i] = open(filenames[i], O_RDWR);
					if (fds[i] < 0)
						continue;
				}
			}
retry:
			mode = open_flags[stress_mwc8modn(SIZEOF_ARRAY(open_flags))];
			fds[i] = open(filenames[i], O_CREAT | O_RDWR | mode, S_IRUSR | S_IWUSR);
			if (fds[i] < 0) {
				switch (errno) {
				case EEXIST:
					fds[i] = open(filenames[i], O_RDWR);
					if (fds[i] < 0)
						continue;
					break;
				case EINVAL:
					retries++;
					if (stress_continue(args) && (retries < 5))
						goto retry;
				default:
					break;
				}
			} else {
				if ((i & 63) == 0)
					(void)shim_fsync(fds[i]);
				if ((i & 511) == 0)
					(void)shim_fdatasync(fds[i]);
			}
		}

		/*
		 *  Close 1 in 8 files before unlinking
		 */
		for (i = 0; i < UNLINK_FILES; i += 8) {
			register const size_t j = idx[i];
			register int fd = fds[j];

			if (fd != -1) {
				(void)close(fd);
				fds[j] = -1;
			}
		}

		/*
		 *  Unlink all files
		 */
		t = stress_time_now();
		for (i = 0, n = 0; i < UNLINK_FILES; i++) {
			if (unlink(filenames[idx[i]]) == 0)
				n++;
		}
		metrics->duration += stress_time_now() - t;
		metrics->count += (double)n;

		stress_unlink_shuffle(idx, mask);

		/*
		 *  Close all the rest of the opened files
		 */
		for (i = 0; i < UNLINK_FILES; i++) {
			register int fd = fds[idx[i]];

			if (fd != -1)
				(void)close(fd);
		}
		if (parent)
			stress_bogo_inc(args);
	} while (stress_continue(args));

	t = stress_time_now();
	for (i = 0; i < UNLINK_FILES; i++) {
		if (unlink(filenames[i]) == 0)
			n++;
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)n;

	for (i = 0; i < UNLINK_FILES; i++) {
		if (fds[i] != -1) {
			if ((i & 127) == 15)
				(void)shim_fsync(fds[i]);
			(void)close(fds[i]);
		}
	}
}
	
/*
 *  stress_unlink
 *	stress unlinking
 */
static int stress_unlink(stress_args_t *args)
{
	int ret, rc = EXIT_SUCCESS;
	char *filenames[UNLINK_FILES];
	char pathname[PATH_MAX];
	pid_t pids[UNLINK_PROCS];
	stress_metrics_t *metrics;
	const size_t metrics_sz = sizeof(*metrics) * (UNLINK_PROCS + 1);
	double duration = 0.0, count = 0.0, rate;

	register size_t i;

	metrics = (stress_metrics_t *)stress_mmap_populate(NULL, metrics_sz,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes for metrics, skipping stressor\n",
			args->name, metrics_sz);
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < UNLINK_PROCS; i++) {
		metrics[i].duration = 0.0;
		metrics[i].count = 0.0;
	}

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc =  stress_exit_status(-ret);
		goto metrics_free;
	}

	(void)memset(filenames, 0, sizeof(filenames));
	for (i = 0; i < UNLINK_FILES; i++) {
		char filename[PATH_MAX + 20];

		(void)snprintf(filename, sizeof(filename), "%s/%c%c%c%c-%4.4zx",
			pathname,
			'a' + stress_mwc8modn(26),
			'a' + stress_mwc8modn(26),
			'a' + stress_mwc8modn(26),
			'a' + stress_mwc8modn(26), i);

		filenames[i] = strdup(filename);
		if (!filenames[i]) {
			pr_inf_skip("%s: failed to allocate filenames, "
				"skipping stressor\n", args->name);
			goto filenames_free;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < UNLINK_PROCS; i++) {
		pids[i] = fork();

		if (pids[i] == 0) {
			stress_unlink_exercise(args, false, &metrics[i], filenames);
			_exit(EXIT_SUCCESS);
		}
	}

	stress_unlink_exercise(args, true, &metrics[UNLINK_PROCS], filenames);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	duration = metrics[UNLINK_PROCS].duration;
	count = metrics[UNLINK_PROCS].count;

	for (i = 0; i < UNLINK_PROCS; i++) {
		const pid_t pid = pids[i];

		if (pid > 1) {
			int status;

			(void)kill(pid, SIGALRM);
			if (shim_waitpid(pid, &status, 0) < 0)
				(void)stress_kill_and_wait(args, pid, SIGKILL, false);

			duration += metrics[i].duration;
			count += metrics[i].duration;
		}
	}

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "unlink calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

filenames_free:
	for (i = 0; i < UNLINK_FILES; i++) {
		if (filenames[i]) {
			(void)unlink(filenames[i]);
			free(filenames[i]);
		}
	}

metrics_free:
	(void)munmap((void *)metrics, metrics_sz);

	(void)stress_temp_dir_rm_args(args);

	return rc;

}

stressor_info_t stress_unlink_info = {
	.stressor = stress_unlink,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help
};
