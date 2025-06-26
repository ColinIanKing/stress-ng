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

#if defined(HAVE_SYS_SENDFILE_H)
#include <sys/sendfile.h>
#else
UNEXPECTED
#endif

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

static const stress_help_t help[] = {
	{ NULL,	"sendfile N",	   "start N workers exercising sendfile" },
	{ NULL,	"sendfile-ops N",  "stop after N bogo sendfile operations" },
	{ NULL,	"sendfile-size N", "size of data to be sent with sendfile" },
	{ NULL,	NULL,		   NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_sendfile_size, "sendfile-size", TYPE_ID_UINT64_BYTES_VM, MIN_SENDFILE_SIZE, MAX_SENDFILE_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE) &&		\
    NEED_GLIBC(2,1,0)

/*
 *  stress_sendfile
 *	stress reading of a temp file and writing to /dev/null via sendfile
 */
static int stress_sendfile(stress_args_t *args)
{
	char filename[PATH_MAX];
	int i = 0, fdin, fdout, ret, bad_fd, rc = EXIT_SUCCESS;
	size_t sz;
	int64_t sendfile_size = DEFAULT_SENDFILE_SIZE;
	double duration = 0.0, bytes = 0.0, rate;
	int metrics_count = 0;

	if (!stress_get_setting("sendfile-size", &sendfile_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sendfile_size = MAX_SENDFILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sendfile_size = MIN_SENDFILE_SIZE;
	}
	sz = (size_t)sendfile_size;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fdin = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_err("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto dir_out;
	}
#if defined(HAVE_POSIX_FALLOCATE)
	ret = shim_posix_fallocate(fdin, (off_t)0, (off_t)sz);
	errno = ret;
#else
	ret = shim_fallocate(fdin, 0, (off_t)0, (off_t)sz);
#endif
	if (ret != 0) {
		rc = stress_exit_status(errno);
		pr_err("%s: fallocate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_in;
	}
	(void)close(fdin);
	if ((fdin = open(filename, O_RDONLY)) < 0) {
		rc = stress_exit_status(errno);
		pr_err("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto dir_out;
	}

	if ((fdout = open("/dev/null", O_WRONLY)) < 0) {
		pr_err("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_in;
	}

	bad_fd = stress_get_bad_fd();

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		off_t offset = 0;
		ssize_t nbytes;
		double t;

		if (LIKELY(metrics_count++ < 1000)) {
			/* fast non-metrics sendfile */
			nbytes = sendfile(fdout, fdin, &offset, sz);
			if (LIKELY(nbytes >= 0))
				goto sendfile_ok;
		} else {
			/* slow metrics sendfile */
			metrics_count = 0;
			t = stress_time_now();
			nbytes = sendfile(fdout, fdin, &offset, sz);
			if (LIKELY(nbytes >= 0)) {
				duration += stress_time_now() - t;
				bytes += (double)nbytes;
				goto sendfile_ok;
			}
		}

		if (errno == ENOSYS) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: skipping stressor, sendfile not implemented\n",
					args->name);
			rc = EXIT_NOT_IMPLEMENTED;
			goto close_out;
		}
		if (errno == EINTR)
			continue;
		pr_fail("%s: sendfile failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto close_out;

sendfile_ok:
		/* Periodically perform some unusual sendfile calls */
		if (UNLIKELY((i++ & 0xff) == 0)) {
			/* Exercise with invalid destination fd */
			offset = 0;
			(void)sendfile(bad_fd, fdin, &offset, sz);

			/* Exercise with invalid source fd */
			offset = 0;
			(void)sendfile(fdout, bad_fd, &offset, sz);

			/* Exercise with invalid offset */
			offset = -1;
			(void)sendfile(fdout, fdin, &offset, sz);

			/* Exercise with invalid size */
			offset = 0;
			(void)sendfile(fdout, fdin, &offset, (size_t)-1);

			/* Exercise with zero size (should work, no-op) */
			offset = 0;
			(void)sendfile(fdout, fdin, &offset, 0);

			/* Exercise with read-only destination (EBADF) */
			offset = 0;
			(void)sendfile(fdin, fdin, &offset, sz);

			/* Exercise with write-only source (EBADF) */
			offset = 0;
			(void)sendfile(fdout, fdout, &offset, sz);

			/* Exercise truncated read */
			offset = (off_t)(sz - 1);
			(void)sendfile(fdout, fdin, &offset, sz);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec sent to /dev/null",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

close_out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fdout);
close_in:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fdin);
	(void)shim_unlink(filename);
dir_out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_sendfile_info = {
	.stressor = stress_sendfile,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_sendfile_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/sendfile.h or sendfile() system call support"
};
#endif
