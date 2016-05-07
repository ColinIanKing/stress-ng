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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

/*
 *  get_opt_ionice_class()
 *	string io scheduler to IOPRIO_CLASS
 */
int32_t get_opt_ionice_class(const char *const str)
{
#if defined(IOPRIO_CLASS_IDLE)
	if (!strcmp("idle", str))
		return IOPRIO_CLASS_IDLE;
#endif
#if defined(IOPRIO_CLASS_BE)
	if (!strcmp("besteffort", str) ||
	    !strcmp("be", str))
		return IOPRIO_CLASS_BE;
#endif
#if defined(IOPRIO_CLASS_RT)
	if (!strcmp("realtime", str) ||
	    !strcmp("rt", str))
		return IOPRIO_CLASS_RT;
#endif
	if (strcmp("which", str))
		fprintf(stderr, "Invalid ionice-class option: %s\n", str);

	fprintf(stderr, "Available options are:");
#if defined(IOPRIO_CLASS_IDLE)
	fprintf(stderr, " idle");
#endif
#if defined(IOPRIO_CLASS_BE)
	fprintf(stderr, " besteffort be");
#endif
#if defined(IOPRIO_CLASS_RT)
	fprintf(stderr, " realtime rt");
#endif
	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

/*
 *  sys_ioprio_set()
 *	ioprio_set system call
 */
int sys_ioprio_set(int which, int who, int ioprio)
{
#if defined(STRESS_IOPRIO) && defined(__NR_ioprio_set)
        return syscall(__NR_ioprio_set, which, who, ioprio);
#else
	(void)which;
	(void)who;
	(void)ioprio;

	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  sys_ioprio_get()
 *	ioprio_get system call
 */
int sys_ioprio_get(int which, int who)
{
#if defined(STRESS_IOPRIO) && defined(__NR_ioprio_get)
        return syscall(__NR_ioprio_get, which, who);
#else
	(void)which;
	(void)who;

	errno = ENOSYS;
	return -1;
#endif
}

#if defined(STRESS_IOPRIO) && defined(__NR_ioprio_set)
/*
 *  set_iopriority()
 *	check ioprio settings and set
 */
void set_iopriority(const int32_t class, const int32_t level)
{
	int data = level, rc;

	switch (class) {
	case UNDEFINED:	/* No preference, don't set */
		return;
	case IOPRIO_CLASS_RT:
	case IOPRIO_CLASS_BE:
		if (level < 0 || level > 7) {
			fprintf(stderr, "Priority levels range from 0 "
				"(max) to 7 (min)\n");
			exit(EXIT_FAILURE);
		}
		break;
	case IOPRIO_CLASS_IDLE:
		if ((level != UNDEFINED) &&
		    (level != 0))
			fprintf(stderr, "Cannot set priority level "
				"with idle, defaulting to 0\n");
		data = 0;
		break;
	default:
		fprintf(stderr, "Unknown priority class: %d\n", class);
		exit(EXIT_FAILURE);
	}
	rc = sys_ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(class, data));
	if (rc < 0) {
		fprintf(stderr, "Cannot set I/O priority: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#else
void set_iopriority(const int32_t class, const int32_t level)
{
	(void)class;
	(void)level;
}
#endif
