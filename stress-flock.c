/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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

#if !defined(__sun__) && defined(LOCK_EX) && defined(LOCK_UN)

/*
 *  stress_flock
 *	stress file locking
 */
int stress_flock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd;
	pid_t ppid = getppid();
	char filename[PATH_MAX];
	char dirname[PATH_MAX];

	/*
	 *  There will be a race to create the directory
	 *  so EEXIST is expected on all but one instance
	 */
	(void)stress_temp_dir(dirname, sizeof(dirname), name, ppid, instance);
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail_err(name, "mkdir");
			return exit_status(errno);
		}
	}

	/*
	 *  Lock file is based on parent pid and instance 0
	 *  as we need to share this among all the other
	 *  stress flock processes
	 */
	(void)stress_temp_filename(filename, sizeof(filename),
		name, ppid, 0, 0);
retry:
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		int rc = exit_status(errno);

		if ((errno == ENOENT) && opt_do_run) {
			/* Race, sometimes we need to retry */
			goto retry;
		}
		/* Not sure why this fails.. report and abort */
		pr_fail_err(name, "open");
		(void)rmdir(dirname);
		return rc;
	}

	do {

		if (flock(fd, LOCK_EX) < 0)
			continue;
		(void)shim_sched_yield();
		(void)flock(fd, LOCK_UN);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return EXIT_SUCCESS;
}

#else

int stress_flock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}
#endif
