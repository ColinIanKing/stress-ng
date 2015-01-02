/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#include <string.h>
#include <errno.h>
#if defined (__linux__)
#include <sys/syscall.h>

/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#define IOPRIO_CLASS_RT 	(1)
#define IOPRIO_CLASS_BE		(2)
#define IOPRIO_CLASS_IDLE	(3)

#define IOPRIO_WHO_PROCESS	(1)
#define IOPRIO_WHO_PGRP		(2)
#define IOPRIO_WHO_USER		(3)

#define IOPRIO_PRIO_VALUE(class, data)	(((class) << 13) | data)
#endif

#include "stress-ng.h"

/*
 *  get_opt_ionice_class()
 *	string io scheduler to IOPRIO_CLASS
 */
int get_opt_ionice_class(const char *const str)
{
#if defined (IOPRIO_CLASS_IDLE)
	if (!strcmp("idle", str))
		return IOPRIO_CLASS_IDLE;
#endif
#if defined (IOPRIO_CLASS_BE)
	if (!strcmp("besteffort", str) ||
	    !strcmp("be", str))
		return IOPRIO_CLASS_BE;
#endif
#if defined (IOPRIO_CLASS_RT)
	if (!strcmp("realtime", str) ||
	    !strcmp("rt", str))
		return IOPRIO_CLASS_RT;
#endif
	if (strcmp("which", str))
		fprintf(stderr, "Invalid ionice-class option: %s\n", str);

	fprintf(stderr, "Available options are:");
#if defined (IOPRIO_CLASS_IDLE)
	fprintf(stderr, " idle");
#endif
#if defined (IOPRIO_CLASS_BE)
	fprintf(stderr, " besteffort be");
#endif
#if defined (IOPRIO_CLASS_RT)
	fprintf(stderr, " realtime rt");
#endif
	fprintf(stderr, "\n");

	exit(EXIT_FAILURE);
}

#if defined (__linux__)
/*
 *  ioprio_set()
 *	ioprio_set system call
 */
static int ioprio_set(const int which, const int who, const int ioprio)
{
        return syscall(SYS_ioprio_set, which, who, ioprio);
}

/*
 *  set_iopriority()
 *	check ioprio settings and set
 */
void set_iopriority(const int class, const int level)
{
	int data = level, rc;

	switch (class) {
	case UNDEFINED:	/* No preference, don't set */
		return;
	case IOPRIO_CLASS_RT:
	case IOPRIO_CLASS_BE:
		if (level < 0 || level > 7) {
			fprintf(stderr, "Priority levels range from 0 (max) to 7 (min)\n");
			exit(EXIT_FAILURE);
		}
		break;
	case IOPRIO_CLASS_IDLE:
		if ((level != UNDEFINED) &&
		    (level != 0))
			fprintf(stderr, "Cannot set priority level with idle, defaulting to 0\n");
		data = 0;
		break;
	default:
		fprintf(stderr, "Unknown priority class: %d\n", class);
		exit(EXIT_FAILURE);
	}
	rc = ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(class, data));
	if (rc < 0) {
		fprintf(stderr, "Cannot set I/O priority: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#else
void set_iopriority(const int class, const int level)
{
	(void)class;
	(void)level;
}
#endif
