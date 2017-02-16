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

static const mode_t modes[] = {
#if defined(S_ISUID)
	S_ISUID,
#endif
#if defined(S_ISGID)
	S_ISGID,
#endif
#if defined(S_ISVTX)
	S_ISVTX,
#endif
#if defined(S_IRUSR)
	S_IRUSR,
#endif
#if defined(S_IWUSR)
	S_IWUSR,
#endif
#if defined(S_IXUSR)
	S_IXUSR,
#endif
#if defined(S_IRGRP)
	S_IRGRP,
#endif
#if defined(S_IWGRP)
	S_IWGRP,
#endif
#if defined(S_IXGRP)
	S_IXGRP,
#endif
#if defined(S_IROTH)
	S_IROTH,
#endif
#if defined(S_IWOTH)
	S_IWOTH,
#endif
#if defined( S_IXOTH)
	S_IXOTH,
#endif
	0
};

/*
 *  BSD systems can return EFTYPE which we can ignore
 *  as a "known" error on invalid chmod mode bits
 */
#if defined(EFTYPE)
#define CHECK(x) if ((x) && (errno != EFTYPE)) return -1
#else
#define CHECK(x) if (x) return -1
#endif

/*
 *  do_fchmod()
 *	fchmod the 4 different masks from a mode flag, namely:
 *		mode flag
 *		all mode flags or'd together
 *		inverse mode flag
 *		inverse all mode flags or'd together
 */
static int do_fchmod(
	const int fd,
	const int i,
	const mode_t mask,
	const mode_t all_mask)
{
	CHECK(fchmod(fd, modes[i]) < 0);
	CHECK(fchmod(fd, mask) < 0);
	CHECK(fchmod(fd, modes[i] ^ all_mask) < 0);
	CHECK(fchmod(fd, mask ^ all_mask) < 0);
	return 0;
}

/*
 *  do_chmod()
 *	chmod the 4 different masks from a mode flag, namely:
 *		mode flag
 *		all mode flags or'd together
 *		inverse mode flag
 *		inverse all mode flags or'd together
 */
static int do_chmod(
	const char *filename,
	const int i,
	const mode_t mask,
	const mode_t all_mask)
{
	CHECK(chmod(filename, modes[i]) < 0);
	CHECK(chmod(filename, mask) < 0);
	CHECK(chmod(filename, modes[i] ^ all_mask) < 0);
	CHECK(chmod(filename, mask ^ all_mask) < 0);
	return 0;
}

/*
 *  stress_chmod
 *	stress chmod
 */
int stress_chmod(const args_t *args)
{
	const pid_t ppid = getppid();
	int i, fd = -1, rc = EXIT_FAILURE, retries = 0;
	mode_t all_mask = 0;
	char filename[PATH_MAX], dirname[PATH_MAX];

	/*
	 *  Allow for multiple workers to chmod the *same* file
	 */
	stress_temp_dir(dirname, sizeof(dirname), args->name, ppid, 0);
	if (mkdir(dirname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = exit_status(errno);
			pr_fail_err("mkdir");
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	if (args->instance == 0) {
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			rc = exit_status(errno);
			pr_fail_err("creat");
			goto tidy;
		}
	} else {
		/* Other instances must try to open the file */
		for (;;) {
			if ((fd = open(filename, O_RDWR, S_IRUSR | S_IWUSR)) > - 1)
				break;

			(void)shim_usleep(100000);
			if (++retries >= 100) {
				pr_err("%s: chmod: file %s took %d "
					"retries to open and gave up "
					"(instance %" PRIu32 ")\n",
					args->name, filename, retries, args->instance);
				goto tidy;
			}
			/* Timed out, then give up */
			if (!g_keep_stressing_flag) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
		}
	}

	for (i = 0; modes[i]; i++)
		all_mask |= modes[i];

	do {
		mode_t mask = 0;

		for (i = 0; modes[i]; i++) {
			mask |= modes[i];
			if (do_fchmod(fd, i, mask, all_mask) < 0) {
				pr_fail_err("fchmod");
			}
			if (do_chmod(filename, i, mask, all_mask) < 0) {
				if (errno == ENOENT || errno == ENOTDIR) {
					/*
					 * File was removed during test by
					 * another worker
					 */
					rc = EXIT_SUCCESS;
					goto tidy;
				}
				pr_fail_err("chmod");
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0) {
		(void)fchmod(fd, 0666);
		(void)close(fd);
	}
	(void)unlink(filename);
	(void)rmdir(dirname);

	return rc;
}
