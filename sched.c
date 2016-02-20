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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && \
    !defined(__OpenBSD__)
#include <sched.h>
#endif

#include "stress-ng.h"

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && \
    !defined(__OpenBSD__)
/*
 *  get_sched_name()
 *	convert sched class to human readable string
 */
static const char *get_sched_name(const int32_t sched)
{
	switch (sched) {
#if defined(SCHED_IDLE)
	case SCHED_IDLE:
		return "idle";
#endif
#if defined(SCHED_FIFO)
	case SCHED_FIFO:
		return "fifo";
#endif
#if defined(SCHED_RR)
	case SCHED_RR:
		return "rr";
#endif
#if defined(SCHED_OTHER)
	case SCHED_OTHER:
		return "other";
#endif
#if defined(SCHED_BATCH)
	case SCHED_BATCH:
		return "batch";
#endif
#if defined(SCHED_DEADLINE)
	case SCHED_DEADLINE:
		return "deadline";
#endif
	default:
		return "unknown";
	}
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && \
     !defined(__OpenBSD__)
/*
 *  set_sched()
 * 	are sched settings valid, if so, set them
 */
void set_sched(const int32_t sched, const int32_t sched_priority)
{
#if defined(SCHED_FIFO) || defined(SCHED_RR)
	int min, max;
#endif
	int rc;
	struct sched_param param;
	const char *name = get_sched_name(sched);

	memset(&param, 0, sizeof(param));

	switch (sched) {
	case UNDEFINED:	/* No preference, don't set */
		return;
#if defined(SCHED_FIFO) || defined(SCHED_RR)
#if defined(SCHED_FIFO)
	case SCHED_FIFO:
#endif
#if defined(SCHED_RR)
	case SCHED_RR:
#endif
		min = sched_get_priority_min(sched);
		max = sched_get_priority_max(sched);

		param.sched_priority = sched_priority;

		if (param.sched_priority == UNDEFINED) {
			if (opt_flags & OPT_FLAGS_AGGRESSIVE)
				param.sched_priority = max;
			else
				param.sched_priority = (max - min) / 2;
			pr_inf(stderr, "priority not given, defaulting to %d\n",
				param.sched_priority);
		}
		if ((param.sched_priority < min) ||
		    (param.sched_priority > max)) {
			fprintf(stderr, "Scheduler priority level must be "
				"set between %d and %d\n",
				min, max);
			exit(EXIT_FAILURE);
		}
		pr_dbg(stderr, "setting scheduler class '%s', priority %d\n",
			name, param.sched_priority);
		break;
#endif
	default:
		param.sched_priority = 0;
		if (sched_priority != UNDEFINED)
			pr_inf(stderr, "ignoring priority level for "
			"scheduler class '%s'\n", name);
		pr_dbg(stderr, "setting scheduler class '%s'\n", name);
		break;
	}
	rc = sched_setscheduler(getpid(), sched, &param);
	if (rc < 0) {
		fprintf(stderr, "Cannot set scheduler: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#else
void set_sched(const int32_t sched, const int32_t sched_priority)
{
	(void)sched;
	(void)sched_priority;
}
#endif

/*
 *  get_opt_sched()
 *	get scheduler policy
 */
int32_t get_opt_sched(const char *const str)
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
#ifdef SCHED_DEADLINE
	if (!strcmp("deadline", str))
		return SCHED_DEADLINE;
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
#ifdef SCHED_DEADLINE
		" deadline"
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
