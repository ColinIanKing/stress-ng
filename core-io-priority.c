/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
		(void)fprintf(stderr, "Invalid ionice-class option: %s\n", str);

	(void)fprintf(stderr, "Available options are:");
#if defined(IOPRIO_CLASS_IDLE)
	(void)fprintf(stderr, " idle");
#endif
#if defined(IOPRIO_CLASS_BE)
	(void)fprintf(stderr, " besteffort be");
#endif
#if defined(IOPRIO_CLASS_RT)
	(void)fprintf(stderr, " realtime rt");
#endif
	(void)fprintf(stderr, "\n");

	_exit(EXIT_FAILURE);
}

#if defined(__NR_ioprio_set)
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
			(void)fprintf(stderr, "Priority levels range from 0 "
				"(max) to 7 (min)\n");
			_exit(EXIT_FAILURE);
		}
		break;
	case IOPRIO_CLASS_IDLE:
		if ((level != UNDEFINED) &&
		    (level != 0))
			(void)fprintf(stderr, "Cannot set priority level "
				"with idle, defaulting to 0\n");
		data = 0;
		break;
	default:
		(void)fprintf(stderr, "Unknown priority class: %d\n", class);
		_exit(EXIT_FAILURE);
	}
	rc = shim_ioprio_set(IOPRIO_WHO_PROCESS, 0,
		IOPRIO_PRIO_VALUE(class, data));
	if ((rc < 0) && (errno != ENOSYS)) {
		(void)fprintf(stderr, "Cannot set I/O priority: errno=%d (%s)\n",
			errno, strerror(errno));
		_exit(EXIT_FAILURE);
	}
}
#else
void set_iopriority(const int32_t class, const int32_t level)
{
	(void)class;
	(void)level;
}
#endif
