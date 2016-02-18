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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_SENDFILE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>


static int64_t opt_sendfile_size = DEFAULT_SENDFILE_SIZE;
static bool set_sendfile_size = false;

void stress_set_sendfile_size(const char *optarg)
{
	set_sendfile_size = true;
	opt_sendfile_size = get_uint64_byte(optarg);
	check_range("sendfile-size", opt_sendfile_size,
		MIN_SENDFILE_SIZE, MAX_SENDFILE_SIZE);
}

/*
 *  stress_sendfile
 *	stress reading of a temp file and writing to /dev/null via sendfile
 */
int stress_sendfile(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char filename[PATH_MAX];
	int fdin, fdout, ret, rc = EXIT_SUCCESS;
	size_t sz;
	const pid_t pid = getpid();

	if (!set_sendfile_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_sendfile_size = MAX_SENDFILE_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_sendfile_size = MIN_SENDFILE_SIZE;
	}
	sz = (size_t)opt_sendfile_size;

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

        (void)umask(0077);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

        if ((fdin = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
                pr_fail_err(name, "open");
		goto dir_out;
        }
	ret = posix_fallocate(fdin, (off_t)0, (off_t)sz);
	if (ret < 0) {
		rc = exit_status(errno);
                pr_fail_err(name, "open");
		goto dir_out;
	}
	if ((fdout = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail_err(name, "open");
		rc = EXIT_FAILURE;
		goto close_in;
	}

	do {
		off_t offset = 0;
		if (sendfile(fdout, fdin, &offset, sz) < 0) {
			if (errno == EINTR)
				continue;
			pr_fail_err(name, "sendfile");
			rc = EXIT_FAILURE;
			goto close_out;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

close_out:
	(void)close(fdout);
close_in:
	(void)close(fdin);
	(void)unlink(filename);
dir_out:
	(void)stress_temp_dir_rm(name, pid, instance);

	return rc;
}
#endif
