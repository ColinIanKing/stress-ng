/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,	"fallocate-ops N",	"stop after N fallocate bogo operations" },
	{ NULL,	"fallocate-bytes N",	"specify size of file to allocate" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_fallocate_bytes(const char *opt)
{
	off_t fallocate_bytes;

	fallocate_bytes = (off_t)stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("fallocate-bytes", fallocate_bytes,
		MIN_FALLOCATE_BYTES, MAX_FALLOCATE_BYTES);
	return stress_set_setting("fallocate-bytes", TYPE_ID_OFF_T, &fallocate_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_fallocate_bytes,	stress_set_fallocate_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_FALLOCATE)

static const int modes[] = {
	0,
#if defined(FALLOC_FL_KEEP_SIZE)
	FALLOC_FL_KEEP_SIZE,
#endif
#if defined(FALLOC_FL_KEEP_SIZE) && defined(FALLOC_FL_PUNCH_HOLE)
	FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
	FALLOC_FL_COLLAPSE_RANGE,
#endif
#if defined(FALLOC_FL_INSERT_RANGE)
	FALLOC_FL_INSERT_RANGE,
#endif
};

/*
 *  illegal mode flags mixes
 */
static const int illegal_modes[] = {
	~0,
#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_PUNCH_HOLE)
	FALLOC_FL_PUNCH_HOLE,
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE) && defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_INSERT_RANGE) && defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_INSERT_RANGE | FALLOC_FL_ZERO_RANGE,
#endif
};

/*
 *  stress_fallocate
 *	stress I/O via fallocate and ftruncate
 */
static int stress_fallocate(const stress_args_t *args)
{
	int fd, ret;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX];
	uint64_t ftrunc_errs = 0;
	off_t fallocate_bytes = DEFAULT_FALLOCATE_BYTES;
	int pipe_fds[2], pipe_ret;

	if (!stress_get_setting("fallocate-bytes", &fallocate_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fallocate_bytes = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fallocate_bytes = MIN_FALLOCATE_BYTES;
	}

	fallocate_bytes /= args->num_instances;
	if (fallocate_bytes < (off_t)MIN_FALLOCATE_BYTES)
		fallocate_bytes = (off_t)MIN_FALLOCATE_BYTES;
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)unlink(filename);

	pipe_ret = pipe(pipe_fds);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_POSIX_FALLOCATE)
		ret = posix_fallocate(fd, (off_t)0, fallocate_bytes);
#else
		ret = shim_fallocate(fd, 0, (off_t)0, fallocate_bytes);
#endif
		if (!keep_stressing_flag())
			break;
		(void)shim_fsync(fd);
		if ((ret == 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail("%s: fstat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			else if (buf.st_size != fallocate_bytes)
				pr_fail("%s: file size %jd does not match size the expected file size of %jd\n",
					args->name, (intmax_t)buf.st_size,
					(intmax_t)fallocate_bytes);
		}

		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!keep_stressing_flag())
			break;
		(void)shim_fsync(fd);

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail("%s: fstat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			else if (buf.st_size != (off_t)0)
				pr_fail("%s: file size %jd does not "
					"match size the expected file size "
					"of 0\n",
					args->name, (intmax_t)buf.st_size);
		}

		if (ftruncate(fd, fallocate_bytes) < 0)
			ftrunc_errs++;
		(void)shim_fsync(fd);
		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		(void)shim_fsync(fd);

		if (SIZEOF_ARRAY(modes) > 1) {
			/*
			 *  non-portable Linux fallocate()
			 */
			int i;

			(void)shim_fallocate(fd, 0, (off_t)0, fallocate_bytes);
			if (!keep_stressing_flag())
				break;
			(void)shim_fsync(fd);

			for (i = 0; i < 64; i++) {
				off_t offset = (stress_mwc64() % fallocate_bytes) & ~0xfff;
				size_t j = (stress_mwc32() >> 8) % SIZEOF_ARRAY(modes);

				(void)shim_fallocate(fd, modes[j], offset, 64 * KB);
				if (!keep_stressing_flag())
					break;
				(void)shim_fsync(fd);
			}
			if (ftruncate(fd, 0) < 0)
				ftrunc_errs++;
			(void)shim_fsync(fd);
		}

		/*
		 *  Exercise fallocate on a bad fd for more kernel
		 *  coverage
		 */
#if defined(HAVE_POSIX_FALLOCATE)
		ret = posix_fallocate(bad_fd, (off_t)0, fallocate_bytes);
#else
		ret = shim_fallocate(bad_fd, 0, (off_t)0, fallocate_bytes);
#endif
		(void)ret;

		/*
		 *  Exercise with various illegal mode flags
		 */
		if (SIZEOF_ARRAY(illegal_modes) > 1) {
			size_t i;

			for (i = 0; i < SIZEOF_ARRAY(illegal_modes); i++) {
				ret = shim_fallocate(fd, illegal_modes[i], (off_t)0, fallocate_bytes);
				(void)ret;
			}
		}

		/*
		 *  fallocate on a pipe is illegal
		 */
		if (pipe_ret == 0) {
			ret = posix_fallocate(pipe_fds[0], (off_t)0, fallocate_bytes);
			(void)ret;
			ret = posix_fallocate(pipe_fds[1], (off_t)0, fallocate_bytes);
			(void)ret;
		}
		/*
		 *  exercise illegal negative offset and lengths
		 */
		ret = posix_fallocate(fd, (off_t)-1, (off_t)0);
		(void)ret;
		ret = posix_fallocate(fd, (off_t)0, (off_t)-1);
		(void)ret;
		ret = posix_fallocate(fd, (off_t)-1, (off_t)-1);
		(void)ret;

		inc_counter(args);
	} while (keep_stressing(args));
	if (ftrunc_errs)
		pr_dbg("%s: %" PRIu64
			" ftruncate errors occurred.\n", args->name, ftrunc_errs);
	if (pipe_ret == 0) {
		(void)close(pipe_fds[0]);
		(void)close(pipe_fds[1]);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_fallocate_info = {
	.stressor = stress_fallocate,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_fallocate_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
