/*
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

static const stress_help_t help[] = {
	{ NULL,	"fsize N",	"start N workers exercising file size limits" },
	{ NULL,	"fsize-ops N",	"stop after N fsize bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_FALLOCATE) &&	\
    defined(RLIMIT_FSIZE) &&	\
    defined(SIGXFSZ)

#define FSIZE_TYPE_FALLOC	(1)
#define FSIZE_TYPE_SIGXFSZ	(2)

static volatile bool sigxfsz;
static uint64_t sigxfsz_count;

static void stress_fsize_handler(int signum)
{
	if (signum == SIGXFSZ) {
		sigxfsz = true;
		sigxfsz_count++;
	}
}

/*
 *  stress_fsize_reported()
 *	check if an issue has already been reported to reduce message
 *	spamming
 */
static bool stress_fsize_reported(const off_t offset, const uint8_t type)
{
	typedef struct {
		off_t offset;
		bool reported;
		uint8_t type;
	} stress_fsize_reported_t;

	static stress_fsize_reported_t reported[sizeof(off_t) * 8 * 4];
	static size_t max;
	size_t i;

	for (i = 0; i < max; i++) {
		if ((reported[i].offset == offset) &&
		    (reported[i].type == type) &&
		    (reported[i].reported))
			return true;
	}
	if (i < SIZEOF_ARRAY(reported)) {
		reported[i].offset = offset;
		reported[i].type = type;
		reported[i].reported = true;
		max++;
	}
	return false;
}

/*
 *  stress_fsize_boundary()
 *	set RLIMIT_FSIZE on offset, test file size up to
 *	offset -1  + size and equal to offset + size
 */
static int stress_fsize_boundary(
	stress_args_t *args,
	const int fd,
	const struct rlimit *old_rlim,
	const off_t offset,
	const off_t size)
{
	struct rlimit new_rlim;
	off_t off;
	int ret, rc = EXIT_SUCCESS;

	if ((rlim_t)offset >= old_rlim->rlim_max)
		return rc;
	if (offset < 1)
		return rc;

	off = (off_t)old_rlim->rlim_max;
	new_rlim.rlim_max = off;
	new_rlim.rlim_cur = offset;

	if (setrlimit(RLIMIT_FSIZE, &new_rlim) < 0) {
		pr_fail("%s: failed to set RLIMIT_FSIZE to %" PRIdMAX " (0x%" PRIxMAX "), errno=%d (%s)\n",
			args->name, (intmax_t)new_rlim.rlim_cur, (intmax_t)new_rlim.rlim_cur,
			errno, strerror(errno));
	}

	sigxfsz = false;
	off = (off_t)new_rlim.rlim_cur - 1;
	ret = shim_fallocate(fd, 0, off, size);
	if (ret < 0) {
		if ((errno != EFBIG) && (errno != ENOSPC) && (errno != EINTR)) {
			pr_fail("%s: fallocate failed at offset %" PRIdMAX" (0x%" PRIxMAX ") with unexpected error, errno=%d (%s)\n",
				args->name, (intmax_t)off, (intmax_t)off,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		return rc;
	}
	if (sigxfsz) {
		pr_fail("%s: got an unexpected SIGXFSZ signal at offset %" PRIdMAX " (0x%" PRIxMAX ")\n",
			args->name, (intmax_t)off, (intmax_t)off);
		return EXIT_FAILURE;
	}
	/* We should be able to fruncate a file to zero bytes */
	if (ftruncate(fd, 0) < 0) {
		pr_inf("%s: truncating file to zero bytes failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	sigxfsz = false;
	off = (off_t)new_rlim.rlim_cur;
	ret = shim_fallocate(fd, 0, off, size);
	if (ret == 0) {
		if (!stress_fsize_reported(off, FSIZE_TYPE_FALLOC)) {
			pr_inf("%s: fallocate unexpectedly succeeded at offset %" PRIdMAX " (0x%" PRIxMAX "), expecting EFBIG error\n",
				args->name, (intmax_t)off, (intmax_t)off);
			return EXIT_FAILURE;
		}
		return rc;
	} else if ((errno != EFBIG) && (errno != ENOSPC) && (errno != EINTR)) {
		pr_fail("%s: fallocate failed at offset %" PRIdMAX" (0x%" PRIxMAX ") with unexpected error, errno=%d (%s)\n",
			args->name, (intmax_t)off, (intmax_t)off,
			errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (!sigxfsz && !stress_fsize_reported(off, FSIZE_TYPE_SIGXFSZ)) {
		pr_inf("%s: did not get expected SIGXFSZ signal at offset %" PRIdMAX " (0x%" PRIxMAX ")\n",
			args->name, (intmax_t)off, (intmax_t)off);
		return EXIT_FAILURE;
	}
	return rc;
}

/*
 *  stress_fsize_max_off_t()
 *	determine max off_t, we can't rely on a POSIXly defined
 *	macro for this, so determine it by the naive method
 */
static off_t stress_fsize_max_off_t(void)
{
	off_t max = 0, prev_max = 0;

	while (max >= 0) {
		prev_max = max;
		max = (max << 1) | 1;
	}
	return prev_max;
}

/*
 *  stress_fsize
 *	stress file size limits
 */
static int stress_fsize(stress_args_t *args)
{
	char filename[PATH_MAX];
	int fd, ret, rc = EXIT_SUCCESS;
	struct rlimit old_rlim;
	rlim_t max;
	const off_t max_offset = stress_fsize_max_off_t();
	double t, duration, rate;

	/* this should work */
	if (getrlimit(RLIMIT_FSIZE, &old_rlim) < 0) {
		pr_fail("%s: getrlimit RLIMIT_FSIZE failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGXFSZ, stress_fsize_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)shim_unlink(filename);

	stress_file_rw_hint_short(fd);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	max = STRESS_MINIMUM(old_rlim.rlim_max, 1024 * 256);
	do {
		struct rlimit new_rlim;
		off_t offset;

		new_rlim.rlim_cur = max;
		new_rlim.rlim_max = old_rlim.rlim_max;

		if (setrlimit(RLIMIT_FSIZE, &new_rlim) < 0) {
			pr_fail("%s: failed to set RLIMIT_FSIZE to %" PRIdMAX " (0x%" PRIxMAX "), errno=%d (%s)\n",
				args->name, (intmax_t)max, (intmax_t)max, errno, strerror(errno));
		}

		/* We should be able to fruncate a file to zero bytes */
		if (ftruncate(fd, 0) < 0) {
			pr_inf("%s: truncating file to zero bytes failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/*
		 *  Test #1, make file 4096 bytes longer than max bytes
		 */
		if (shim_fallocate(fd, 0, 0, (off_t)max) < 0) {
			if ((errno == ENOSPC) || (errno == EINTR)) {
				/* No resource */
				pr_inf_skip("%s: allocating file to %" PRIdMAX " (0x%" PRIxMAX ") bytes failed, errno=%d (%s), "
					"skipping stressor\n", args->name, (intmax_t)max, (intmax_t)max,
					errno, strerror(errno));
				rc = EXIT_NO_RESOURCE;
			} else {
				/* A real issue, report it */
				pr_inf("%s: allocating file to %" PRIdMAX " bytes (0x%" PRIxMAX ") failed, errno=%d (%s), "
					"terminating stressor\n", args->name, (intmax_t)max, (intmax_t)max,
					errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			break;
		}
		sigxfsz = false;
		if (shim_fallocate(fd, 0, (off_t)max, 4096) == 0) {
			pr_fail("%s: fallocate unexpectedly succeeded at offset %" PRIdMAX " (0x%" PRIxMAX "), expecting EFBIG error\n",
				args->name, (intmax_t)max, (intmax_t)max);
			rc = EXIT_FAILURE;
		} else if ((errno != EFBIG) && (errno != ENOSPC) && (errno != EINTR)) {
			pr_fail("%s: failed at offset %" PRIdMAX " (0x%" PRIxMAX ") with unexpected error, errno=%d (%s)\n",
				args->name, (intmax_t)max, (intmax_t)max, errno, strerror(errno) );
			rc = EXIT_FAILURE;
		}
		if (!sigxfsz) {
			pr_fail("%s: expected a SIGXFSZ signal at offset %" PRIdMAX " (0x%" PRIxMAX "), nothing happened\n",
				args->name, (intmax_t)max, (intmax_t)max);
			rc = EXIT_FAILURE;
		}

		/*
		 *  Test #2, test for allocation 0..offset and file offset..max
		 */
		offset = (off_t)stress_mwc32modn((uint32_t)max);
		offset = (offset == 0) ? 1 : offset;

		if (ftruncate(fd, 0) < 0) {
			pr_inf("%s: truncating file to zero bytes failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (stress_fsize_boundary(args, fd, &old_rlim, offset, max - offset) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}

		/* Should be able to set back to original size */
		new_rlim = old_rlim;
		if (setrlimit(RLIMIT_FSIZE, &new_rlim) < 0) {
			pr_fail("%s: failed to set RLIMIT_FSIZE to %" PRIdMAX " (0x%" PRIxMAX "), errno=%d (%s)\n",
				args->name, (intmax_t)new_rlim.rlim_cur, (intmax_t)new_rlim.rlim_cur,
				errno, strerror(errno));
			rc = EXIT_FAILURE;
		}

		/*
		 *  Test #3, work through all off_t sizes in powers of 2 - 1
		 */
		for (offset = 1; offset < max_offset; offset = (offset << 1 | 1)) {
			if (ftruncate(fd, 0) < 0) {
				pr_inf("%s: truncating file to zero bytes failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto err;
			}
			if (stress_fsize_boundary(args, fd, &old_rlim, offset, 1) == EXIT_FAILURE) {
				rc = EXIT_FAILURE;
				goto err;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

err:
	duration = stress_time_now() - t;
	rate = (duration > 0.0) ? (double)sigxfsz_count / duration : 0.0;
	stress_metrics_set(args, 0, "SIGXFSZ signals per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (fd != -1)
		(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_fsize_info = {
	.stressor = stress_fsize,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_fsize_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fallocate(), RLIMIT_FSIZE or SIGXFSZ"
};
#endif
