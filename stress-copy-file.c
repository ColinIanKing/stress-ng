// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

static int stress_set_copy_file_bytes(const char *opt)
{
	uint64_t copy_file_bytes;

	copy_file_bytes = stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("copy-file-bytes", copy_file_bytes,
		MIN_COPY_FILE_BYTES, MAX_COPY_FILE_BYTES);
	return stress_set_setting("copy-file-bytes", TYPE_ID_UINT64, &copy_file_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_copy_file_bytes,	stress_set_copy_file_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_COPY_FILE_RANGE)

#define COPY_FILE_MAX_BUF_SIZE	(4096)

/*
 *  stress_copy_file_fill()
 *	fill chunk of file with random value
 */
static int stress_copy_file_fill(
	const stress_args_t *args,
	const int fd,
	const off_t off,
	const ssize_t size)
{
	char buf[COPY_FILE_MAX_BUF_SIZE];
	ssize_t sz = size;

	(void)shim_memset(buf, stress_mwc8(), sizeof(buf));

	if (lseek(fd, off, SEEK_SET) < 0)
		return -1;

	while (sz > 0) {
		ssize_t n;

		n = write(fd, buf, sizeof(buf));
		if (n < 0)
			return -1;
		sz -= n;
		if (!stress_continue(args))
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
	off_t *off_in,
	const int fd_out,
	off_t *off_out,
	const ssize_t bytes)
{
	size_t bytes_left = (size_t)bytes;

	while (bytes_left > 0) {
		ssize_t n, bytes_in, bytes_out;
		size_t sz = STRESS_MINIMUM(bytes_left, COPY_FILE_MAX_BUF_SIZE);

		char buf_in[COPY_FILE_MAX_BUF_SIZE];
		char buf_out[COPY_FILE_MAX_BUF_SIZE];

#if defined(HAVE_PREAD)
		bytes_in = pread(fd_in, buf_in, sz, *off_in);
		if (bytes_in == 0)
			return 0;
		if (bytes_in < 0)
			break;
		bytes_out = pread(fd_out, buf_out, sz, *off_out);
		if (bytes_out == 0)
			return 0;
		if (bytes_out <= 0)
			break;
#else
		off_t off_ret;

		off_ret = lseek(fd_in, *off_in, SEEK_SET);
		if (off_ret != *off_in)
			return -1;
		off_ret = lseek(fd_out, *off_out, SEEK_SET);
		if (off_ret != *off_out)
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
#endif

		n = STRESS_MINIMUM(bytes_in, bytes_out);
		if (shim_memcmp(buf_in, buf_out, (size_t)n) != 0)
			return -1;
		bytes_left -= n;
		*off_in += n;
		*off_out += n;
	}
	if (bytes_left > 0)
		return -1;
	return 0;
}

/*
 *  stress_copy_file
 *	stress reading chunks of file using copy_file_range()
 */
static int stress_copy_file(const stress_args_t *args)
{
	int fd_in, fd_out, rc = EXIT_FAILURE, ret;
	const int fd_bad = stress_get_bad_fd();
	char filename[PATH_MAX - 5], tmp[PATH_MAX];
	uint64_t copy_file_bytes = DEFAULT_COPY_FILE_BYTES;
	double duration = 0.0, bytes = 0.0, rate;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (!stress_get_setting("copy-file-bytes", &copy_file_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			copy_file_bytes = MAX_COPY_FILE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			copy_file_bytes = MIN_COPY_FILE_BYTES;
	}

	copy_file_bytes /= args->num_instances;
	if (copy_file_bytes < DEFAULT_COPY_FILE_SIZE)
		copy_file_bytes = DEFAULT_COPY_FILE_SIZE * 2;
	if (copy_file_bytes < MIN_COPY_FILE_BYTES)
		copy_file_bytes = MIN_COPY_FILE_BYTES;

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
	(void)shim_unlink(tmp);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t copy_ret;
		shim_loff_t off_in, off_out, off_in_orig, off_out_orig;
		double t;

		off_in_orig = (shim_loff_t)stress_mwc64modn(copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off_in = off_in_orig;
		off_out_orig = (shim_loff_t)stress_mwc64modn(copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off_out = off_out_orig;

		if (!stress_continue(args))
			break;
		stress_copy_file_fill(args, fd_in, (off_t)off_in, DEFAULT_COPY_FILE_SIZE);
		if (!stress_continue(args))
			break;
		t = stress_time_now();
		copy_ret = shim_copy_file_range(fd_in, &off_in, fd_out,
						&off_out, DEFAULT_COPY_FILE_SIZE, 0);
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
			off_in = off_in_orig;
			off_out = off_out_orig;

			verify_ret = stress_copy_file_range_verify(fd_in, &off_in,
					fd_out, &off_out, copy_ret);
			if (verify_ret < 0) {
				pr_fail("%s: copy_file_range verify failed, input offset=%jd, output offset=%jd\n",
					args->name, (intmax_t)off_in_orig, (intmax_t)off_out_orig);
			}
		}
		if (!stress_continue(args))
			break;

		/*
		 *  Exercise with bad fds
		 */
		VOID_RET(ssize_t, shim_copy_file_range(fd_bad, &off_in, fd_out,
						&off_out, DEFAULT_COPY_FILE_SIZE, 0));
		VOID_RET(ssize_t, shim_copy_file_range(fd_in, &off_in, fd_bad,
						&off_out, DEFAULT_COPY_FILE_SIZE, 0));
		VOID_RET(ssize_t, shim_copy_file_range(fd_out, &off_in, fd_in,
						&off_out, DEFAULT_COPY_FILE_SIZE, 0));
		(void)copy_ret;
		/*
		 *  Exercise with bad flags
		 */
		VOID_RET(ssize_t, shim_copy_file_range(fd_in, &off_in, fd_out,
						&off_out, DEFAULT_COPY_FILE_SIZE, ~0U));
		if (!stress_continue(args))
			break;
		(void)shim_fsync(fd_out);
		stress_bogo_inc(args);
	} while (stress_continue(args));
	rc = EXIT_SUCCESS;

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec copy rate", rate / (double)MB);

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

stressor_info_t stress_copy_file_info = {
	.stressor = stress_copy_file,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_copy_file_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without clone() system call"
};
#endif
