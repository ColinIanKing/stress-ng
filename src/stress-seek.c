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
	{ NULL,	"seek N",	"start N workers performing random seek r/w IO" },
	{ NULL,	"seek-ops N",	"stop after N seek bogo operations" },
	{ NULL,	"seek-punch",	"punch random holes in file to stress extents" },
	{ NULL,	"seek-size N",	"length of file to do random I/O upon" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_seek_size(const char *opt)
{
	uint64_t seek_size;

	seek_size = stress_get_uint64_byte(opt);
	stress_check_range_bytes("seek-size", seek_size,
		MIN_SEEK_SIZE, MAX_SEEK_SIZE);
	return stress_set_setting("seek-size", TYPE_ID_UINT64, &seek_size);
}


static int stress_set_seek_punch(const char *opt)
{
	(void)opt;
	bool seek_punch = true;

	return stress_set_setting("seek-punch", TYPE_ID_BOOL, &seek_punch);
}

static inline off_t max_off_t(void)
{
	off_t v, nv = 1;

	for (v = 1; (nv = ((v << 1) | 1)) > 0; v = nv)
		;

	return v;
}

/*
 *  stress_seek
 *	stress I/O via random seeks and read/writes
 */
static int stress_seek(const stress_args_t *args)
{
	uint64_t len;
	uint64_t seek_size = DEFAULT_SEEK_SIZE;
	int ret, fd, rc = EXIT_FAILURE;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX];
	uint8_t buf[512];
	const off_t bad_off_t = max_off_t();
#if defined(OPT_SEEK_PUNCH)
	bool seek_punch = false;

	(void)stress_get_setting("seek-punch", TYPE_ID_BOOL, &seek_punch);
#endif

	if (!stress_get_setting("seek-size", &seek_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			seek_size = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			seek_size = MIN_SEEK_SIZE;
	}
	len = seek_size - sizeof(buf);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	stress_strnrnd((char *)buf, sizeof(buf));

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto finish;
	}
	(void)unlink(filename);
	/* Generate file with hole at the end */
	if (lseek(fd, (off_t)len, SEEK_SET) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: lseek failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_finish;
	}
	if (write(fd, buf, sizeof(buf)) < 0) {
		if (errno == ENOSPC) {
			rc = EXIT_NO_RESOURCE;
		} else {
			rc = exit_status(errno);
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		goto close_finish;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		off_t offset;
		uint8_t tmp[512];
		ssize_t rwret;

		offset = stress_mwc64() % len;
		if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
			pr_fail("%s: lseek failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto close_finish;
		}
re_write:
		if (!keep_stressing_flag())
			break;
		rwret = write(fd, buf, sizeof(buf));
		if (rwret <= 0) {
			if (errno == ENOSPC)
				goto do_read;
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_write;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto close_finish;
			}
		}

do_read:
		offset = stress_mwc64() % len;
		if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
			pr_fail("%s: lseek SEEK_SET failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto close_finish;
		}
re_read:
		if (!keep_stressing_flag())
			break;
		rwret = read(fd, tmp, sizeof(tmp));
		if (rwret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_read;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto close_finish;
			}
		}
		if ((rwret != sizeof(tmp)) &&
		    (g_opt_flags & OPT_FLAGS_VERIFY)) {
			pr_fail("%s: incorrect read size, expecting 512 bytes\n", args->name);
		}
#if defined(SEEK_END)
		if (lseek(fd, 0, SEEK_END) < 0) {
			if (errno != EINVAL)
				pr_fail("%s: lseek SEEK_END failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#endif
#if defined(SEEK_CUR)
		if (lseek(fd, 0, SEEK_CUR) < 0) {
			if (errno != EINVAL)
				pr_fail("%s: lseek SEEK_CUR failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#endif
#if defined(SEEK_HOLE) &&	\
    !defined(__APPLE__)
		if (lseek(fd, 0, SEEK_HOLE) < 0) {
			if (errno != EINVAL)
				pr_fail("%s: lseek SEEK_HOLE failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#endif
#if defined(SEEK_DATA) &&	\
    !defined(__APPLE__)
		if (lseek(fd, 0, SEEK_DATA) < 0) {
			if (errno != EINVAL)
				pr_fail("%s: lseek SEEK_DATA failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#endif
#if defined(SEEK_HOLE) &&	\
    defined(SEEK_DATA) &&	\
    !defined(__APPLE__)
		{
			int i;

			offset = stress_mwc64() % seek_size;
			for (i = 0; i < 20 && keep_stressing(args); i++) {
				offset = lseek(fd, offset, SEEK_DATA);
				if (offset < 0)
					break;
				offset = lseek(fd, offset, SEEK_HOLE);
				if (offset < 0)
					break;
			}
		}
#endif
#if defined(OPT_SEEK_PUNCH)
		if (!seek_punch_hole)
			continue;

		offset = stress_mwc64() % len;
		if (shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE |
				  FALLOC_FL_KEEP_SIZE, offset, 8192) < 0) {
			if (errno == EOPNOTSUPP)
				seek_punch_hole = false;
		}
#endif
		/*
		 *  Exercise lseek on an invalid fd
		 */
		offset = stress_mwc64() % len;
		offset = lseek(bad_fd, (off_t)offset, SEEK_SET);
		(void)offset;

		/*
		 *  Exercise lseek with invalid offsets, EINVAL
		 */
		offset = lseek(fd, ~(off_t)0, SEEK_SET);
		(void)offset;
		offset = lseek(fd, bad_off_t, SEEK_SET);
		(void)offset;
		offset = lseek(fd, bad_off_t, SEEK_CUR);
		(void)offset;
		offset = lseek(fd, bad_off_t, SEEK_END);
		(void)offset;
		/*
		 *  Exercise lseek with invalid offsets, ENXIO
		 */
#if defined(SEEK_DATA) && !defined(__APPLE__)
		offset = lseek(fd, len + sizeof(buf) + 1, SEEK_DATA);
		(void)offset;
#endif
#if defined(SEEK_HOLE) && !defined(__APPLE__)
		offset = lseek(fd, len + sizeof(buf) + 1, SEEK_HOLE);
		(void)offset;
#endif
		/*
		 *  Exercise lseek with invalid whence
		 */
		offset = lseek(fd, 0, ~0);
		(void)offset;

		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;
close_finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_seek_size,	stress_set_seek_size },
	{ OPT_seek_punch,	stress_set_seek_punch },
	{ 0,			NULL }
};

stressor_info_t stress_seek_info = {
	.stressor = stress_seek,
	.class = CLASS_IO | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
