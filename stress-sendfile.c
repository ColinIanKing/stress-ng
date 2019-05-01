/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"sendfile N",	   "start N workers exercising sendfile" },
	{ NULL,	"sendfile-ops N",  "stop after N bogo sendfile operations" },
	{ NULL,	"sendfile-size N", "size of data to be sent with sendfile" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_sendfile_size(const char *opt)
{
	int64_t sendfile_size;

	sendfile_size = get_uint64_byte(opt);
	check_range_bytes("sendfile-size", sendfile_size,
		MIN_SENDFILE_SIZE, MAX_SENDFILE_SIZE);
	return set_setting("sendfile-size", TYPE_ID_UINT64, &sendfile_size);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sendfile_size,	stress_set_sendfile_size },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_SENDFILE_H) &&	\
    NEED_GLIBC(2,1,0)

/*
 *  stress_sendfile
 *	stress reading of a temp file and writing to /dev/null via sendfile
 */
static int stress_sendfile(const args_t *args)
{
	char filename[PATH_MAX];
	int fdin, fdout, ret, rc = EXIT_SUCCESS;
	size_t sz;
	int64_t sendfile_size = DEFAULT_SENDFILE_SIZE;

	if (!get_setting("sendfile-size", &sendfile_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sendfile_size = MAX_SENDFILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sendfile_size = MIN_SENDFILE_SIZE;
	}
	sz = (size_t)sendfile_size;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	if ((fdin = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto dir_out;
	}
#if defined(HAVE_POSIX_FALLOCATE)
	ret = posix_fallocate(fdin, (off_t)0, (off_t)sz);
#else
	ret = shim_fallocate(fdin, 0, (off_t)0, (off_t)sz);
#endif
	if (ret < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto dir_out;
	}
	if ((fdout = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail_err("open");
		rc = EXIT_FAILURE;
		goto close_in;
	}

	do {
		off_t offset = 0;
		if (sendfile(fdout, fdin, &offset, sz) < 0) {
			if (errno == ENOSYS) {
				pr_inf("%s: skipping stressor, sendfile not implemented\n",
					args->name);
				rc = EXIT_NOT_IMPLEMENTED;
				goto close_out;
			}
			if (errno == EINTR)
				continue;
			pr_fail_err("sendfile");
			rc = EXIT_FAILURE;
			goto close_out;
		}
		inc_counter(args);
	} while (keep_stressing());

close_out:
	(void)close(fdout);
close_in:
	(void)close(fdin);
	(void)unlink(filename);
dir_out:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_sendfile_info = {
	.stressor = stress_sendfile,
	.class = CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sendfile_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
