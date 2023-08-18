// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#define MIN_FALLOCATE_BYTES	(1 * MB)
#define MAX_FALLOCATE_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_FALLOCATE_BYTES	(1 * GB)

static const stress_help_t help[] = {
	{ NULL,	"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,	"fallocate-bytes N",	"specify size of file to allocate" },
	{ NULL,	"fallocate-ops N",	"stop after N fallocate bogo operations" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_fallocate_bytes(const char *opt)
{
	off_t fallocate_bytes;

	fallocate_bytes = (off_t)stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("fallocate-bytes", (uint64_t)fallocate_bytes,
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
#if defined(FALLOC_FL_KEEP_SIZE) &&	\
    defined(FALLOC_FL_PUNCH_HOLE)
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
#if defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_PUNCH_HOLE)
	FALLOC_FL_PUNCH_HOLE,
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE) &&	\
    defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_INSERT_RANGE) &&	\
    defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_INSERT_RANGE | FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_UNSHARE_RANGE) && \
    defined(FALLOC_FL_KEEP_SIZE)
	FALLOC_FL_UNSHARE_RANGE | FALLOC_FL_KEEP_SIZE,
#endif
};

/*
 *  stress_fallocate
 *	stress I/O via fallocate and ftruncate
 */
static int stress_fallocate(const stress_args_t *args)
{
	int fd = -1, ret, pipe_ret = -1, pipe_fds[2] = { -1, -1 };
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX];
	uint64_t ftrunc_errs = 0;
	off_t fallocate_bytes = DEFAULT_FALLOCATE_BYTES;
	int *mode_perms = NULL, all_modes;
	size_t i, mode_count;
	const char *fs_type;

	for (all_modes = 0, i = 0; i < SIZEOF_ARRAY(modes); i++)
		all_modes |= modes[i];
	mode_count = stress_flag_permutation(all_modes, &mode_perms);

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
	if (ret < 0) {
		free(mode_perms);
		return stress_exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		free(mode_perms);
		return ret;
	}
	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	pipe_ret = pipe(pipe_fds);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_POSIX_FALLOCATE)
		ret = shim_posix_fallocate(fd, (off_t)0, fallocate_bytes);
#else
		ret = shim_fallocate(fd, 0, (off_t)0, fallocate_bytes);
#endif
		if (!stress_continue_flag())
			break;
		(void)shim_fsync(fd);
		if ((ret == 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail("%s: fstat failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
			else if (buf.st_size != fallocate_bytes)
				pr_fail("%s: file size %jd does not match size the expected file size of %jd\n",
					args->name, (intmax_t)buf.st_size,
					(intmax_t)fallocate_bytes);
		}

		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!stress_continue_flag())
			break;
		(void)shim_fsync(fd);
		if (!stress_continue_flag())
			break;

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
		if (!stress_continue_flag())
			break;
		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!stress_continue_flag())
			break;
		(void)shim_fsync(fd);
		if (!stress_continue_flag())
			break;

		if (SIZEOF_ARRAY(modes) > 1) {
			/*
			 *  non-portable Linux fallocate()
			 */
			(void)shim_fallocate(fd, 0, (off_t)0, fallocate_bytes);
			if (!stress_continue_flag())
				break;
			(void)shim_fsync(fd);
			if (!stress_continue_flag())
				break;

			for (i = 0; i < 64; i++) {
				const size_t j = stress_mwc32modn((uint32_t)SIZEOF_ARRAY(modes));
				const off_t offset = (off_t)stress_mwc64modn((uint64_t)fallocate_bytes) & ~0xfff;

				if (shim_fallocate(fd, modes[j], offset, 64 * KB) == 0)
					(void)shim_fsync(fd);
				if (!stress_continue_flag())
					break;
			}
			/* Exercise all the mode permutations, most will fail */
			for (i = 0; i < mode_count; i++) {
				const off_t offset = (off_t)stress_mwc64modn((uint64_t)fallocate_bytes) & ~0xfff;

				if (shim_fallocate(fd, mode_perms[i], offset, 64 * KB) == 0)
					(void)shim_fsync(fd);
				if (!stress_continue_flag())
					break;
			}
			if (ftruncate(fd, 0) < 0)
				ftrunc_errs++;
			if (!stress_continue_flag())
				break;
			(void)shim_fsync(fd);
			if (!stress_continue_flag())
				break;
		}

		/*
		 *  Exercise fallocate on a bad fd for more kernel
		 *  coverage
		 */
#if defined(HAVE_POSIX_FALLOCATE)
		VOID_RET(int, shim_posix_fallocate(bad_fd, (off_t)0, fallocate_bytes));
#else
		VOID_RET(int, shim_fallocate(bad_fd, 0, (off_t)0, fallocate_bytes));
#endif
		if (!stress_continue_flag())
			break;

		/*
		 *  Exercise with various illegal mode flags
		 */
		if (SIZEOF_ARRAY(illegal_modes) > 1) {
			for (i = 0; i < SIZEOF_ARRAY(illegal_modes); i++) {
				VOID_RET(int, shim_fallocate(fd, illegal_modes[i], (off_t)0, fallocate_bytes));
				if (!stress_continue_flag())
					break;
			}
		}

		/*
		 *  fallocate on a pipe is illegal
		 */
		if (pipe_ret == 0) {
			VOID_RET(int, shim_posix_fallocate(pipe_fds[0], (off_t)0, fallocate_bytes));
			VOID_RET(int, shim_posix_fallocate(pipe_fds[1], (off_t)0, fallocate_bytes));
		}
		/*
		 *  exercise illegal negative offset and lengths
		 */
		VOID_RET(int, shim_posix_fallocate(fd, (off_t)-1, (off_t)0));
		if (!stress_continue_flag())
			break;
		VOID_RET(int, shim_posix_fallocate(fd, (off_t)0, (off_t)-1));
		if (!stress_continue_flag())
			break;
		VOID_RET(int, shim_posix_fallocate(fd, (off_t)-1, (off_t)-1));

		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (ftrunc_errs)
		pr_dbg("%s: %" PRIu64
			" ftruncate errors occurred.\n", args->name, ftrunc_errs);
	if (pipe_ret == 0) {
		(void)close(pipe_fds[0]);
		(void)close(pipe_fds[1]);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd != -1)
		(void)close(fd);

	(void)stress_temp_dir_rm_args(args);
	free(mode_perms);

	return EXIT_SUCCESS;
}

stressor_info_t stress_fallocate_info = {
	.stressor = stress_fallocate,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_fallocate_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without fallocate() system call"
};
#endif
