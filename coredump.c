/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <semaphore.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) || defined (__linux__)
#include <sched.h>
#endif
#if defined (__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#include "stress-ng.h"

#if defined (__linux__)
/*
 *  set_coredump()
 *	limit what is coredumped because
 *	potentially we could have hugh dumps
 *	with the vm and mmap tests
 */
void set_coredump(const char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), "/proc/%d/coredump_filter", getpid());
	if ((fd = open(path, O_WRONLY)) >= 0) {
		const char *str = "0x00";
		ssize_t n = write(fd, str, strlen(str));
		close(fd);

		if (n < 0)
			pr_failed_dbg(name, "can't set coredump_filter");
		else
			return;
	}
}
#else
void set_coredump(const char *name)
{
	(void)name;
}
#endif
