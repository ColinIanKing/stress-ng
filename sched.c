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
#include <unistd.h>
#include <errno.h>
#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && !defined(__OpenBSD__)
#include <sched.h>
#endif

#include "stress-ng.h"

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && !defined(__OpenBSD__)
/*
 *  set_sched()
 * 	are sched settings valid, if so, set them
 */
void set_sched(const int sched, const int sched_priority)
{
#if defined (SCHED_FIFO) || defined (SCHED_RR)
	int min, max;
#endif
	int rc;
	struct sched_param param;

	switch (sched) {
	case UNDEFINED:	/* No preference, don't set */
		return;
#if defined (SCHED_FIFO) || defined (SCHED_RR)
	case SCHED_FIFO:
	case SCHED_RR:
		min = sched_get_priority_min(sched);
		max = sched_get_priority_max(sched);
		if ((sched_priority == UNDEFINED) ||
		    (sched_priority > max) ||
		    (sched_priority < min)) {
			fprintf(stderr, "Scheduler priority level must be set between %d and %d\n",
				min, max);
			exit(EXIT_FAILURE);
		}
		param.sched_priority = sched_priority;
		break;
#endif
	default:
		if (sched_priority != UNDEFINED)
			fprintf(stderr, "Cannot set sched priority for chosen scheduler, defaulting to 0\n");
		param.sched_priority = 0;
	}
	pr_dbg(stderr, "setting scheduler class %d, priority %d\n",
		sched, param.sched_priority);
	rc = sched_setscheduler(getpid(), sched, &param);
	if (rc < 0) {
		fprintf(stderr, "Cannot set scheduler priority: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#else
void set_sched(const int sched, const int sched_priority)
{
	(void)sched;
	(void)sched_priority;
}
#endif

/*
 *  get_opt_sched()
 *	get scheduler policy
 */
int get_opt_sched(const char *const str)
{
#ifdef SCHED_OTHER
	if (!strcmp("other", str))
		return SCHED_OTHER;
#endif
#ifdef SCHED_BATCH
	if (!strcmp("batch", str))
		return SCHED_BATCH;
#endif
#ifdef SCHED_IDLE
	if (!strcmp("idle", str))
		return SCHED_IDLE;
#endif
#ifdef SCHED_FIFO
	if (!strcmp("fifo", str))
		return SCHED_FIFO;
#endif
#ifdef SCHED_RR
	if (!strcmp("rr", str))
		return SCHED_RR;
#endif
	if (strcmp("which", str))
		fprintf(stderr, "Invalid sched option: %s\n", str);
	fprintf(stderr, "Available scheduler options are:"
#ifdef SCHED_OTHER
		" other"
#endif
#ifdef SCHED_BATCH
		" batch"
#endif
#ifdef SCHED_IDLE
		" idle"
#endif
#ifdef SCHED_FIFO
		" fifo"
#endif
#ifdef SCHED_FIFO
		" rr"
#endif
		"\n");
	exit(EXIT_FAILURE);
}
