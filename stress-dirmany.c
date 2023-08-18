// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#define MIN_DIRMANY_BYTES     (0)
#define MAX_DIRMANY_BYTES     (MAX_FILE_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"dirmany N",		"start N directory file populating stressors" },
	{ NULL, "dirmany-filesize" ,	"specify size of files (default 0)" },
	{ NULL,	"dirmany-ops N",	"stop after N directory file bogo operations" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_dirmany_bytes()
 *      set size of files to be created
 */
static int stress_set_dirmany_bytes(const char *opt)
{
	off_t dirmany_bytes;

	dirmany_bytes = (off_t)stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("dirmany-bytes", (uint64_t)dirmany_bytes,
		MIN_DIRMANY_BYTES, MAX_DIRMANY_BYTES);
	return stress_set_setting("dirmany-bytes", TYPE_ID_OFF_T, &dirmany_bytes);
}

static void stress_dirmany_filename(
	const char *pathname,
	const size_t pathname_len,
	char *filename,
	const size_t filename_sz,
	const size_t filename_len,
	const uint64_t n)
{
	if (pathname_len + filename_len + 18 < filename_sz) {
		char *ptr = filename;

		(void)shim_memcpy(ptr, pathname, pathname_len);
		ptr += pathname_len;
		*ptr++ = '/';
		(void)shim_memset(ptr, 'x', filename_len);
		ptr += filename_len;
		(void)snprintf(ptr, sizeof(filename) + (size_t)(ptr - filename), "%16.16" PRIx64, n);
	} else {
		(void)snprintf(filename, filename_sz, "%16.16" PRIx64, n);
	}
}

static uint64_t stress_dirmany_create(
	const stress_args_t *args,
	const char *pathname,
	const size_t pathname_len,
	const off_t dirmany_bytes,
	const uint64_t i_start,
	double *create_time,
	size_t *max_len,
	uint64_t *total_created,
	bool *failed)
{
	const double t_now = stress_time_now();
	const double t_left = args->time_end - t_now;
	/* Assume create takes 60%, remove takes 40% of run time */
	const double t_end = t_now + (t_left * 0.60);
	uint64_t i_end = i_start;
	size_t filename_len = 1;

	*max_len = 256;

	while (stress_continue(args)) {
		struct stat statbuf;
		char filename[PATH_MAX + 20];
		int fd;

		if ((LIKELY(g_opt_timeout > 0)) && stress_time_now() > t_end)
			break;

		stress_dirmany_filename(pathname, pathname_len, filename, sizeof(filename), filename_len, i_end);
		fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			if (errno == ENAMETOOLONG) {
				filename_len--;
				*max_len = filename_len;
				continue;
			}
			break;
		}
		if (filename_len < *max_len)
			filename_len++;
		i_end++;
		if (dirmany_bytes > 0) {
#if defined(HAVE_POSIX_FALLOCATE)
			VOID_RET(int, shim_posix_fallocate(fd, (off_t)0, dirmany_bytes));
#else
			VOID_RET(int, shim_fallocate(fd, 0, (off_t)0, dirmany_bytes));
#endif
		}
		if ((i_end & 0xff) == 0xff)
			shim_fsync(fd);
		(void)close(fd);

		/* File should really exist */
		if ((stat(pathname, &statbuf) < 0) && (errno != ENOMEM)) {
			pr_fail("%s: stat failed on file %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			*failed = true;
			break;
		}
		(*total_created)++;

		stress_bogo_inc(args);
	}

	*create_time += (stress_time_now() - t_now);

	return i_end;
}

static void stress_dirmany_remove(
	const char *pathname,
	const size_t pathname_len,
	const uint64_t i_start,
	uint64_t i_end,
	double *remove_time,
	const size_t max_len)
{
	uint64_t i;
	const double t_now = stress_time_now();
	size_t filename_len = 1;

	for (i = i_start; i < i_end; i++) {
		char filename[PATH_MAX + 20];

		stress_dirmany_filename(pathname, pathname_len, filename, sizeof(filename), filename_len, i);
		(void)shim_unlink(filename);
		if (filename_len < max_len)
			filename_len++;
	}
	*remove_time += (stress_time_now() - t_now);
}

/*
 *  stress_dirmany
 *	stress directory with many empty files
 */
static int stress_dirmany(const stress_args_t *args)
{
	int ret;
	uint64_t i_start = 0, total_created = 0;
	char pathname[PATH_MAX];
	double create_time = 0.0, remove_time = 0.0, total_time = 0.0;
	off_t dirmany_bytes = 0;
	size_t pathname_len;

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);
	pathname_len = strlen(pathname);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_get_setting("dirmany-bytes", &dirmany_bytes);

	if (args->instance == 0) {
		char sz[32];

		pr_dbg("%s: %s byte file size\n", args->name,
			dirmany_bytes ? stress_uint64_to_str(sz, sizeof(sz), (uint64_t)dirmany_bytes) : "0");
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t i_end;
		size_t max_len;
		bool failed = false;

		i_end = stress_dirmany_create(args, pathname, pathname_len,
				dirmany_bytes, i_start,
				&create_time, &max_len, &total_created, &failed);
		if (failed) {
			ret = EXIT_FAILURE;
			break;
		}
		stress_dirmany_remove(pathname, pathname_len,
				i_start, i_end, &remove_time, max_len);
		i_start = i_end;

		/* Avoid i_start wraparound */
		if (i_start > 1000000000)
			i_start = 0;
	} while (stress_continue(args));

	total_time = create_time + remove_time;
	if ((total_created > 0) && (total_time > 0.0)) {
		double rate;

		stress_metrics_set(args, 0, "% of time creating files", create_time / total_time * 100.0);
		stress_metrics_set(args, 1, "% of time removing file", remove_time / total_time * 100.0);

		rate = (create_time > 0.0) ? (double)total_created / create_time : 0.0;
		stress_metrics_set(args, 2, "files created per sec", rate);
		rate = (remove_time > 0.0) ? (double)total_created / remove_time : 0.0;
		stress_metrics_set(args, 3, "files removed per sec", rate);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (total_created == 0)
		pr_warn("%s: no files were created in %s\n", args->name, pathname);

	(void)stress_temp_dir_rm_args(args);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_dirmany_bytes,	stress_set_dirmany_bytes },
	{ 0,			NULL }
};

stressor_info_t stress_dirmany_info = {
	.stressor = stress_dirmany,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
