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

#define MIN_COPY_FILE_BYTES	(128 * MB)
#define MAX_COPY_FILE_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_COPY_FILE_BYTES	(256 * MB)
#define DEFAULT_COPY_FILE_SIZE	(128 * KB)

static const stress_help_t help[] = {
	{ NULL,	"copy-file N",		"start N workers that copy file data" },
	{ NULL,	"copy-file-bytes N",	"specify size of file to be copied" },
	{ NULL,	"copy-file-ops N",	"stop after N copy bogo operations" },
	{ NULL,	NULL,			NULL }

};

static const stress_opt_t opts[] = {
	{ OPT_copy_file_bytes, "copy-file-bytes", TYPE_ID_UINT64_BYTES_FS, MIN_COPY_FILE_BYTES, MAX_COPY_FILE_BYTES, NULL },
	END_OPT,
};

#if defined(HAVE_COPY_FILE_RANGE)

#define COPY_FILE_MAX_BUF_SIZE	(4096)

/*
 *  stress_copy_file_seek64()
 *	seek with off64_t
 */
static inline shim_off64_t stress_copy_file_seek64(int fd, shim_off64_t off64, int whence)
{
#if defined(HAVE_LSEEK64)
	return lseek64(fd, off64, whence);
#else
	/* max signed off_t without causing signed overflow */
	const off_t max_off64 = ((((off_t)1U << ((sizeof(off_t) * 8) - 2)) - 1) * 2 + 1);

	if (off64 > max_off64) {
		errno = EINVAL;
		return (shim_off64_t)-1;
	}
	return (shim_off64_t)lseek(fd, (off_t)off64, whence);
#endif
}

/*
 *  stress_copy_file_fill()
 *	fill chunk of file with random value
 */
static int stress_copy_file_fill(
	stress_args_t *args,
	const int fd,
	const shim_off64_t off64,
	const ssize_t size)
{
	char buf[COPY_FILE_MAX_BUF_SIZE];
	ssize_t sz = size;

	(void)shim_memset(buf, stress_mwc8(), sizeof(buf));

	if (stress_copy_file_seek64(fd, off64, SEEK_SET) < 0)
		return -1;

	while (sz > 0) {
		ssize_t n;

		n = write(fd, buf, sizeof(buf));
		if (n < 0)
			return -1;
		sz -= n;
		if (UNLIKELY(!stress_continue(args)))
			return 0;
	}
	return 0;
}

/*
 *  stress_copy_file_range_verify()
 *	verify copy file from fd_in to fd_out worked correctly for
 *	--verify option
 */
static int stress_copy_file_range_verify(
	const int fd_in,
	shim_off64_t *off64_in,
	const int fd_out,
	shim_off64_t *off64_out,
	const ssize_t bytes)
{
	size_t bytes_left = (size_t)bytes;

	while (bytes_left > 0) {
		ssize_t n, bytes_in, bytes_out;
		size_t sz = STRESS_MINIMUM(bytes_left, COPY_FILE_MAX_BUF_SIZE);
		char buf_in[COPY_FILE_MAX_BUF_SIZE];
		char buf_out[COPY_FILE_MAX_BUF_SIZE];
		shim_off64_t off_ret;

		off_ret = stress_copy_file_seek64(fd_in, *off64_in, SEEK_SET);
		if (off_ret != *off64_in)
			return -1;
		off_ret = stress_copy_file_seek64(fd_out, *off64_out, SEEK_SET);
		if (off_ret != *off64_out)
			return -1;
		bytes_in = read(fd_in, buf_in, sz);
		if (bytes_in == 0)
			return 0;
		if (bytes_in < 0)
			break;
		bytes_out = read(fd_out, buf_out, sz);
		if (bytes_out == 0)
			return 0;
		if (bytes_out <= 0)
			break;

		n = STRESS_MINIMUM(bytes_in, bytes_out);
		if (shim_memcmp(buf_in, buf_out, (size_t)n) != 0)
			return -1;
		bytes_left -= n;
		*off64_in += n;
		*off64_out += n;
	}
	if (bytes_left > 0)
		return -1;
	return 0;
}

/*
 *  stress_copy_file
 *	stress reading chunks of file using copy_file_range()
 */
static int stress_copy_file(stress_args_t *args)
{
	int fd_in, fd_out, rc = EXIT_FAILURE, ret;
	const int fd_bad = stress_get_bad_fd();
	char filename[PATH_MAX - 5], tmp[PATH_MAX];
	uint64_t copy_file_bytes, copy_file_bytes_total = DEFAULT_COPY_FILE_BYTES;
	double duration = 0.0, bytes = 0.0, rate;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (!stress_get_setting("copy-file-bytes", &copy_file_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			copy_file_bytes_total = MAX_COPY_FILE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			copy_file_bytes_total = MIN_COPY_FILE_BYTES;
	}
	if (copy_file_bytes_total < MIN_COPY_FILE_BYTES) {
		copy_file_bytes_total = MIN_COPY_FILE_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --copy-file-bytes too small, using %" PRIu64 " instead\n",
				args->name, copy_file_bytes_total);
	}
	if (copy_file_bytes_total > MAX_COPY_FILE_BYTES) {
		copy_file_bytes_total = MAX_COPY_FILE_BYTES;
		if (stress_instance_zero(args))
			pr_inf("%s: --copy-file-bytes too large, using %" PRIu64 " instead\n",
				args->name, copy_file_bytes_total);
	}

	copy_file_bytes = copy_file_bytes_total / args->instances;
	if (copy_file_bytes < DEFAULT_COPY_FILE_SIZE) {
		copy_file_bytes = DEFAULT_COPY_FILE_SIZE * 2;
		copy_file_bytes_total = copy_file_bytes * args->instances;
	}
	if (copy_file_bytes < MIN_COPY_FILE_BYTES) {
		copy_file_bytes = MIN_COPY_FILE_BYTES;
		copy_file_bytes_total = copy_file_bytes * args->instances;
	}
	if (stress_instance_zero(args))
		stress_fs_usage_bytes(args, copy_file_bytes, copy_file_bytes_total);

        ret = stress_temp_dir_mk_args(args);
        if (ret < 0)
                return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
			filename, sizeof(filename), stress_mwc32());
	(void)snprintf(tmp, sizeof(tmp), "%s-orig", filename);
	if ((fd_in = open(tmp, O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, tmp, errno, strerror(errno));
		goto tidy_dir;
	}
	(void)shim_unlink(tmp);
	if (ftruncate(fd_in, (off_t)copy_file_bytes) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: ftruncated failed, errno=%d (%s)%s\n",
			args->name, errno, strerror(errno),
			stress_get_fs_type(tmp));
		goto tidy_in;
	}

	(void)snprintf(tmp, sizeof(tmp), "%s-copy", filename);
	if ((fd_out = open(tmp, O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, tmp, errno, strerror(errno));
		goto tidy_in;
	}

#if defined(HAVE_PATHCONF)
	/* Exercise some pathconf options */
#if defined(_PC_REC_INCR_XFER_SIZE)
	VOID_RET(long int, pathconf(tmp, _PC_REC_INCR_XFER_SIZE));
#endif
#if defined(_PC_REC_MAX_XFER_SIZE)
	VOID_RET(long int, pathconf(tmp, _PC_REC_MAX_XFER_SIZE));
#endif
#if defined(_PC_REC_MIN_XFER_SIZE)
	VOID_RET(long int, pathconf(tmp, _PC_REC_MIN_XFER_SIZE));
#endif
#if defined(_PC_REC_XFER_ALIGN)
	VOID_RET(long int, pathconf(tmp, _PC_REC_XFER_ALIGN));
#endif
#endif
	(void)shim_unlink(tmp);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t copy_ret;
		shim_off64_t off64_in, off64_out, off64_in_orig, off64_out_orig;
		double t;

		off64_in_orig = (shim_loff_t)stress_mwc64modn(copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off64_in = off64_in_orig;
		off64_out_orig = (shim_loff_t)stress_mwc64modn(copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off64_out = off64_out_orig;

		if (UNLIKELY(!stress_continue(args)))
			break;
		stress_copy_file_fill(args, fd_in, off64_in, DEFAULT_COPY_FILE_SIZE);
		if (UNLIKELY(!stress_continue(args)))
			break;
		t = stress_time_now();
		copy_ret = shim_copy_file_range(fd_in, &off64_in, fd_out,
						&off64_out, DEFAULT_COPY_FILE_SIZE, 0);
		if (LIKELY(copy_ret >= 0)) {
			duration += stress_time_now() - t;
			bytes += (double)copy_ret;
		} else {
			if ((errno == EAGAIN) ||
			    (errno == EINTR) ||
			    (errno == ENOSPC))
				continue;
			if (errno == ENOSYS) {
				pr_inf_skip("%s: copy_file_range failed, system "
					"call not implemented, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				goto tidy_out;
			}
			if (errno == EINVAL) {
				pr_inf_skip("%s: copy_file_range failed, the "
					"kernel splice may be not implemented "
					"for the file system, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				goto tidy_out;
			}
			pr_fail("%s: copy_file_range failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_out;
		}
		if (verify) {
			int verify_ret;
			off64_in = off64_in_orig;
			off64_out = off64_out_orig;

			verify_ret = stress_copy_file_range_verify(fd_in, &off64_in,
					fd_out, &off64_out, copy_ret);
			if (verify_ret < 0) {
				pr_fail("%s: copy_file_range verify failed, input offset=%" PRIdMAX " output offset=%" PRIdMAX "\n",
					args->name, (intmax_t)off64_in_orig, (intmax_t)off64_out_orig);
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise with bad fds
		 */
		VOID_RET(ssize_t, shim_copy_file_range(fd_bad, &off64_in, fd_out,
						&off64_out, DEFAULT_COPY_FILE_SIZE, 0));
		VOID_RET(ssize_t, shim_copy_file_range(fd_in, &off64_in, fd_bad,
						&off64_out, DEFAULT_COPY_FILE_SIZE, 0));
		VOID_RET(ssize_t, shim_copy_file_range(fd_out, &off64_in, fd_in,
						&off64_out, DEFAULT_COPY_FILE_SIZE, 0));
		(void)copy_ret;
		/*
		 *  Exercise with bad flags
		 */
		VOID_RET(ssize_t, shim_copy_file_range(fd_in, &off64_in, fd_out,
						&off64_out, DEFAULT_COPY_FILE_SIZE, ~0U));
		if (UNLIKELY(!stress_continue(args)))
			break;
		(void)shim_fsync(fd_out);
		stress_bogo_inc(args);
	} while (stress_continue(args));
	rc = EXIT_SUCCESS;

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec copy rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

tidy_out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_out);
tidy_in:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_in);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_copy_file_info = {
	.stressor = stress_copy_file,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_copy_file_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without clone() system call"
};
#endif
