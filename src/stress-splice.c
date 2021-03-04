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
	{ NULL,	"splice N",	  "start N workers reading/writing using splice" },
	{ NULL,	"splice-ops N",	  "stop after N bogo splice operations" },
	{ NULL,	"splice-bytes N", "number of bytes to transfer per splice call" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_splice_bytes(const char *opt)
{
	size_t splice_bytes;

	splice_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("splice-bytes", splice_bytes,
		MIN_SPLICE_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("splice-bytes", TYPE_ID_SIZE_T, &splice_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_splice_bytes,	stress_set_splice_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_SPLICE) &&	\
    defined(SPLICE_F_MOVE)

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
static int stress_splice(const stress_args_t *args)
{
	int fd_in, fd_out, fds1[2], fds2[2];
	size_t splice_bytes = DEFAULT_SPLICE_BYTES;
	int rc = EXIT_FAILURE;

	if (!stress_get_setting("splice-bytes", &splice_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			splice_bytes = MAX_SPLICE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			splice_bytes = MIN_SPLICE_BYTES;
	}
	splice_bytes /= args->num_instances;
	if (splice_bytes < MIN_SPLICE_BYTES)
		splice_bytes = MIN_SPLICE_BYTES;

	if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
		pr_fail("%s: open /dev/zero failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_done;
	}

	/*
	 *   /dev/zero -> pipe splice -> pipe splice -> /dev/null
	 */
	if (pipe(fds1) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fd_in;
	}

	if (pipe(fds2) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds1;
	}

	if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds2;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
		loff_t off_in, off_out;

		ret = splice(fd_in, NULL, fds1[1], NULL,
			splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		ret = splice(fds1[0], NULL, fds2[1], NULL,
			splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;
		ret = splice(fds2[0], NULL, fd_out, NULL,
			splice_bytes, SPLICE_F_MOVE);
		if (ret < 0)
			break;

		/* Exercise -ESPIPE errors */
		off_in = 1;
		off_out = 1;
		ret = splice(fds1[0], &off_in, fds1[1], &off_out,
			4096, SPLICE_F_MOVE);
		(void)ret;

		off_out = 1;
		ret = splice(fd_in, NULL, fds1[1], &off_out,
			splice_bytes, SPLICE_F_MOVE);
		(void)ret;

		off_in = 1;
		ret = splice(fds1[0], &off_in, fd_out, NULL,
			splice_bytes, SPLICE_F_MOVE);
		(void)ret;

		/* Exercise no-op splice of zero size */
		ret = splice(fd_in, NULL, fds1[1], NULL,
			0, SPLICE_F_MOVE);
		(void)ret;

		/* Exercise invalid splice flags */
		ret = splice(fd_in, NULL, fds1[1], NULL,
			1, ~0);
		(void)ret;

		/* Exercise 1 byte splice, zero flags */
		ret = splice(fd_in, NULL, fds1[1], NULL,
			1, 0);
		(void)ret;

		/* Exercise splicing to oneself */
		off_in = 0;
		off_out = 0;
		ret = splice(fds1[1], &off_in, fds1[1], &off_out,
			4096, SPLICE_F_MOVE);
		(void)ret;

		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_out);
close_fds2:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds2[0]);
	(void)close(fds2[1]);
close_fds1:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds1[0]);
	(void)close(fds1[1]);
close_fd_in:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_in);
close_done:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_splice_info = {
	.stressor = stress_splice,
	.class = CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_splice_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_PIPE_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
