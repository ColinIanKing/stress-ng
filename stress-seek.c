/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

static uint64_t opt_seek_size = DEFAULT_SEEK_SIZE;
static bool set_seek_size = false;

void stress_set_seek_size(const char *optarg)
{
	set_seek_size = true;
	opt_seek_size = get_uint64_byte(optarg);
	check_range_bytes("seek-size", opt_seek_size,
		MIN_SEEK_SIZE, MAX_SEEK_SIZE);
}

/*
 *  stress_seek
 *	stress I/O via random seeks and read/writes
 */
int stress_seek(const args_t *args)
{
	uint64_t len;
	int ret, fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	uint8_t buf[512];
#if defined(OPT_SEEK_PUNCH)
	bool punch_hole = true;
#endif

	if (!set_seek_size) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_seek_size = MAX_SEEK_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_seek_size = MIN_SEEK_SIZE;
	}
	len = opt_seek_size - sizeof(buf);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	stress_strnrnd((char *)buf, sizeof(buf));

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto finish;
	}
	(void)unlink(filename);
	/* Generate file with hole at the end */
	if (lseek(fd, (off_t)len, SEEK_SET) < 0) {
		rc = exit_status(errno);
		pr_fail_err("lseek");
		goto close_finish;
	}
	if (write(fd, buf, sizeof(buf)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("write");
		goto close_finish;
	}

	do {
		off_t offset;
		uint8_t tmp[512];
		ssize_t rwret;

		offset = mwc64() % len;
		if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
			pr_fail_err("lseek");
			goto close_finish;
		}
re_write:
		if (!g_keep_stressing_flag)
			break;
		rwret = write(fd, buf, sizeof(buf));
		if (rwret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_write;
			if (errno) {
				pr_fail_err("write");
				goto close_finish;
			}
		}

		offset = mwc64() % len;
		if (lseek(fd, (off_t)offset, SEEK_SET) < 0) {
			pr_fail_err("lseek SEEK_SET");
			goto close_finish;
		}
re_read:
		if (!g_keep_stressing_flag)
			break;
		rwret = read(fd, tmp, sizeof(tmp));
		if (rwret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto re_read;
			if (errno) {
				pr_fail_err("read");
				goto close_finish;
			}
		}
		if ((rwret != sizeof(tmp)) &&
		    (g_opt_flags & OPT_FLAGS_VERIFY)) {
			pr_fail("%s: incorrect read size, expecting 512 bytes", args->name);
		}
#if defined(SEEK_END)
		if (lseek(fd, 0, SEEK_END) < 0) {
			if (errno != EINVAL)
				pr_fail_err("lseek SEEK_END");
		}
#endif
#if defined(SEEK_CUR)
		if (lseek(fd, 0, SEEK_CUR) < 0) {
			if (errno != EINVAL)
				pr_fail_err("lseek SEEK_CUR");
		}
#endif
#if defined(SEEK_HOLE)
		if (lseek(fd, 0, SEEK_HOLE) < 0) {
			if (errno != EINVAL)
				pr_fail_err("lseek SEEK_HOLE");
		}
#endif
#if defined(SEEK_DATA)
		if (lseek(fd, 0, SEEK_DATA) < 0) {
			if (errno != EINVAL)
				pr_fail_err("lseek SEEK_DATA");
		}
#endif

#if defined(OPT_SEEK_PUNCH)
		if (!punch_hole)
			continue;

		offset = mwc64() % len;
		if (shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE |
				  FALLOC_FL_KEEP_SIZE, offset, 8192) < 0) {
			if (errno == EOPNOTSUPP)
				punch_hole = false;
		}
#endif
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
close_finish:
	(void)close(fd);
finish:
	(void)stress_temp_dir_rm_args(args);
	return rc;
}
