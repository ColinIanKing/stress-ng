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

#define MIN_SEEK_SIZE		(1 * MB)
#define MAX_SEEK_SIZE		(MAX_FILE_LIMIT)
#define DEFAULT_SEEK_SIZE	(16 * MB)

static double duration;
static double count;
static double seek_count;

static const stress_help_t help[] = {
	{ NULL,	"seek N",	"start N workers performing random seek r/w IO" },
	{ NULL,	"seek-ops N",	"stop after N seek bogo operations" },
	{ NULL,	"seek-punch",	"punch random holes in file to stress extents" },
	{ NULL,	"seek-size N",	"length of file to do random I/O upon" },
	{ NULL,	NULL,		NULL }
};

static inline off_t max_off_t(void)
{
	off_t v, nv = 1;

	for (v = 1; (nv = ((v << 1) | 1)) > 0; v = nv)
		;

	return v;
}

static off_t stress_shim_lseek(int fd, off_t offset, int whence)
{
	off_t ret;
	static int metrics_count = 0;

	if (LIKELY(metrics_count++ < 1000)) {
		ret = lseek(fd, offset, whence);
		seek_count++;
	} else {
		double t;

		metrics_count = 0;
		t = stress_time_now();
		ret = lseek(fd, offset, whence);
		if (LIKELY(ret >= 0)) {
			duration += stress_time_now() - t;
			count++;
			seek_count++;
		}
	}
	return ret;
}

/*
 *  stress_seek
 *	stress I/O via random seeks and read/writes
 */
static int stress_seek(stress_args_t *args)
{
	uint64_t len;
	uint64_t seek_size = DEFAULT_SEEK_SIZE;
	int ret, fd, rc = EXIT_FAILURE;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX];
	uint8_t buf[512] ALIGN64;
	const off_t bad_off_t = max_off_t();
	const char *fs_type;
#if defined(HAVE_OFF64_T) &&	\
    defined(HAVE_LSEEK64)
	off64_t	offset64 = (off64_t)0;
#endif
#if defined(FALLOC_FL_PUNCH_HOLE)
	bool seek_punch = false;

	(void)stress_get_setting("seek-punch", &seek_punch);
#endif
	count = 0.0;
	seek_count = 0.0;

	if (!stress_get_setting("seek-size", &seek_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			seek_size = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			seek_size = MIN_SEEK_SIZE;
	}
	if (seek_size < MIN_SEEK_SIZE) {
		seek_size = MIN_SEEK_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --seek-size too small, using %" PRIu64 " instead\n",
				args->name, seek_size);
	}
	if (seek_size > MAX_SEEK_SIZE) {
		seek_size = MAX_SEEK_SIZE;
		if (stress_instance_zero(args))
			pr_inf("%s: --seek-size too large, using %" PRIu64 " instead\n",
				args->name, seek_size);
	}

	len = seek_size - sizeof(buf);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_rndbuf(buf, sizeof(buf));

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto finish;
	}
	stress_file_rw_hint_short(fd);

	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);
	/* Generate file with hole at the end */
	if (stress_shim_lseek(fd, (off_t)len, SEEK_SET) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		goto close_finish;
	}
	if (write(fd, buf, sizeof(buf)) < 0) {
		if (errno == ENOSPC) {
			rc = EXIT_NO_RESOURCE;
		} else {
			rc = stress_exit_status(errno);
			pr_fail("%s: write failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
		}
		goto close_finish;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		off_t offset;
		uint8_t tmp[512] ALIGN64;
		ssize_t rwret;

		offset = (off_t)stress_mwc64modn(len);
		if (UNLIKELY(stress_shim_lseek(fd, (off_t)offset, SEEK_SET) < 0)) {
			pr_fail("%s: lseek failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto close_finish;
		}
re_write:
		if (UNLIKELY(!stress_continue_flag()))
			break;
		rwret = write(fd, buf, sizeof(buf));
		if (UNLIKELY(rwret <= 0)) {
			if (errno == ENOSPC)
				goto do_read;
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_write;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}

do_read:
		offset = (off_t)stress_mwc64modn(len);
		if (UNLIKELY(stress_shim_lseek(fd, (off_t)offset, SEEK_SET) < 0)) {
			pr_fail("%s: lseek SEEK_SET failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno), fs_type);
			rc = EXIT_FAILURE;
			goto close_finish;
		}
re_read:
		if (UNLIKELY(!stress_continue_flag()))
			break;
		rwret = read(fd, tmp, sizeof(tmp));
		if (UNLIKELY(rwret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_read;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
		if (UNLIKELY((rwret != sizeof(tmp)) &&
		    (g_opt_flags & OPT_FLAGS_VERIFY))) {
			pr_fail("%s: incorrect read size, expecting 512 bytes\n", args->name);
			rc = EXIT_FAILURE;
			goto close_finish;
		}
#if defined(SEEK_END)
		if (UNLIKELY(stress_shim_lseek(fd, 0, SEEK_END) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek SEEK_END failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SEEK_CUR)
		if (UNLIKELY(stress_shim_lseek(fd, 0, SEEK_CUR) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek SEEK_CUR failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_OFF64_T) &&	\
    defined(HAVE_LSEEK64)
#if defined(SEEK_SET)
		/*
		 *  exercise 64 bit lseek, on 64 arches this calls lseek,
		 *  but it is worth checking anyhow just in case it is
		 *  broken on 32 or 64 bit systems
		 */
		if (UNLIKELY(lseek64(fd, offset64, SEEK_SET) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek64 SEEK_SET failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SEEK_END)
		if (UNLIKELY(lseek64(fd, offset64, SEEK_END) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek64 SEEK_END failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SEEK_CUR)
		if (UNLIKELY(lseek64(fd, offset64, SEEK_CUR) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek64 SEEK_CUR failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#endif
#if defined(SEEK_HOLE) &&	\
    !defined(__APPLE__)
		if (UNLIKELY(stress_shim_lseek(fd, 0, SEEK_HOLE) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek SEEK_HOLE failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SEEK_DATA) &&	\
    !defined(__APPLE__)
		if (UNLIKELY(stress_shim_lseek(fd, 0, SEEK_DATA) < 0)) {
			if (errno != EINVAL) {
				pr_fail("%s: lseek SEEK_DATA failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto close_finish;
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SEEK_HOLE) &&	\
    defined(SEEK_DATA) &&	\
    !defined(__APPLE__)
		{
			int i;

			offset = (off_t)stress_mwc64modn(seek_size);
			for (i = 0; LIKELY((i < 20) && stress_continue(args)); i++) {
				offset = stress_shim_lseek(fd, offset, SEEK_DATA);
				if (UNLIKELY(offset < 0))
					break;
				offset = stress_shim_lseek(fd, offset, SEEK_HOLE);
				if (UNLIKELY(offset < 0))
					break;
			}
		}
#endif
#if defined(FALLOC_FL_PUNCH_HOLE)
		if (!seek_punch)
			goto inc;

		offset = (off_t)stress_mwc64modn(len);
		if (UNLIKELY(shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE |
				  FALLOC_FL_KEEP_SIZE, offset, 8192) < 0)) {
			if (errno == EOPNOTSUPP)
				seek_punch = false;
		}
#else
		UNEXPECTED
#endif
		/*
		 *  Exercise lseek on an invalid fd
		 */
		offset = (off_t)stress_mwc64modn(len);
		VOID_RET(off_t, lseek(bad_fd, (off_t)offset, SEEK_SET));

		/*
		 *  Exercise lseek with invalid offsets, EINVAL
		 */
		VOID_RET(off_t, lseek(fd, ~(off_t)0, SEEK_SET));
		VOID_RET(off_t, lseek(fd, bad_off_t, SEEK_SET));
		VOID_RET(off_t, lseek(fd, bad_off_t, SEEK_CUR));
		VOID_RET(off_t, lseek(fd, bad_off_t, SEEK_END));
		/*
		 *  Exercise lseek with invalid offsets, ENXIO
		 */
#if defined(SEEK_DATA) &&	\
    !defined(__APPLE__)
		VOID_RET(off_t, lseek(fd, (off_t)(len + sizeof(buf) + 1), SEEK_DATA));
#endif
#if defined(SEEK_HOLE) &&	\
    !defined(__APPLE__)
		VOID_RET(off_t, lseek(fd, (off_t)(len + sizeof(buf) + 1), SEEK_HOLE));
#endif
		/*
		 *  Exercise lseek with invalid whence
		 */
		VOID_RET(off_t, lseek(fd, 0, ~0));

#if defined(FALLOC_FL_PUNCH_HOLE)
inc:
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
close_finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
finish:
	duration = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per seek",
		duration * 1000000000, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "seeks", seek_count, STRESS_METRIC_TOTAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_seek_size,  "seek-size",  TYPE_ID_UINT64_BYTES_FS, MIN_SEEK_SIZE, MAX_SEEK_SIZE, NULL },
	{ OPT_seek_punch, "seek-punch", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_seek_info = {
	.stressor = stress_seek,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
