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

#if defined(STRESS_THRASH)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

static pid_t pid;

static int pagein_proc(const pid_t pid)
{
	char path[PATH_MAX];
	char buffer[4096];
	int fdmem;
	FILE *fpmap;
	const size_t page_size = stress_get_pagesize();
	size_t pages = 0;
	int traced = 0;

	snprintf(path, sizeof(path), "/proc/%d/mem", pid);
	fdmem = open(path, O_RDONLY);
	if (fdmem < 0)
		return -errno;

	snprintf(path, sizeof(path), "/proc/%d/maps", pid);
	fpmap = fopen(path, "r");
	if (!fpmap) {
		close(fdmem);
		return -errno;
	}

	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (fgets(buffer, sizeof(buffer), fpmap)) {
		off_t off;
		uintmax_t begin, end;
		uint8_t byte;
		if (sscanf(buffer, "%jx-%jx", &begin, &end) != 2)
			continue;

		traced++;
		for (off = (off_t)begin; off < (off_t)end; off += page_size, pages++) {
			if (lseek(fdmem, off, SEEK_SET) == (off_t)-1)
				continue;
			if (read(fdmem, &byte, sizeof(byte)) == sizeof(byte)) {
				;
			}
		}
	}

	(void)fclose(fpmap);
	(void)close(fdmem);

	return 0;
}

static int pagein_all_procs(void)
{
	DIR *dp;
	struct dirent *d;

	dp = opendir("/proc");
	if (!dp)
		return -1;

	while ((d = readdir(dp)) != NULL) {
		pid_t pid;

		if (isdigit(d->d_name[0]) &&
                    sscanf(d->d_name, "%d", &pid) == 1) {
			pagein_proc(pid);
		}
	}
	(void)closedir(dp);

	return 0;
}

int thrash_start(void)
{
	if (geteuid() != 0) {
		pr_inf(stderr, "not running as root, ignoring --thrash option\n");
		return -1;
	}
	if (pid) {
		pr_err(stderr, "thrash background process already started\n");
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		pr_err(stderr, "thrash background process failed to fork: %d (%s)\n",
			errno, strerror(errno));
		return -1;
	} else if (pid == 0) {
		while (opt_do_run) {
			pagein_all_procs();
			sleep(1);
		}
		_exit(0);
	}
	return 0;
}

void thrash_stop(void)
{
	int status;

	if (!pid)
		return;

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);

	pid = 0;
}

#else
int thrash_start(void)
{
	return 0;
}

void thrash_stop(void)
{
}
#endif
