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

#if defined(STRESS_THRASH)

static pid_t thrash_pid;

/*
 *  pagein_proc()
 *	force pages into memory for a given process
 */
static int pagein_proc(const pid_t pid)
{
	char path[PATH_MAX];
	char buffer[4096];
	int fdmem;
	FILE *fpmap;
	const size_t page_size = stress_get_pagesize();
	size_t pages = 0;

	(void)snprintf(path, sizeof(path), "/proc/%d/mem", pid);
	fdmem = open(path, O_RDONLY);
	if (fdmem < 0)
		return -errno;

	(void)snprintf(path, sizeof(path), "/proc/%d/maps", pid);
	fpmap = fopen(path, "r");
	if (!fpmap) {
		(void)close(fdmem);
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
		/* Ignore bad range */
		if (begin >= end)
			continue;
		/* Skip huge ranges more than 2GB */
		if (end - begin > 0x80000000UL)
			continue;
		/* Skip bad start */
		if (end == 0)
			continue;

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

/*
 *  pagein_all_procs()
 *	force pages into memory for all processes
 */
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

/*
 *  thrash_start()
 *	start paging in thrash process
 */
int thrash_start(void)
{
	if (geteuid() != 0) {
		pr_inf("not running as root, ignoring --thrash option\n");
		return -1;
	}
	if (thrash_pid) {
		pr_err("thrash background process already started\n");
		return -1;
	}
	thrash_pid = fork();
	if (thrash_pid < 0) {
		pr_err("thrash background process failed to fork: %d (%s)\n",
			errno, strerror(errno));
		return -1;
	} else if (thrash_pid == 0) {
#if defined(SCHED_RR)
		set_sched(SCHED_RR, 10);
#endif
		while (g_keep_stressing_flag) {
			pagein_all_procs();
			sleep(1);
		}
		_exit(0);
	}
	return 0;
}

/*
 *  thrash_stop()
 *	stop paging in thrash process
 */
void thrash_stop(void)
{
	int status;

	if (!thrash_pid)
		return;

	(void)kill(thrash_pid, SIGKILL);
	(void)waitpid(thrash_pid, &status, 0);

	thrash_pid = 0;
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
