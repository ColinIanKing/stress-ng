/*
 * Copyright (C) 2022-2025 Colin Ian King
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
 */
#ifndef CORE_PERF_H
#define CORE_PERF_H

/* perf related constants */
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H) &&	\
    defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
#define STRESS_PERF_MAX		(128 + 32)

/* per perf counter info */
typedef struct {
	uint64_t counter;		/* perf counter */
	int	 fd;			/* perf per counter fd */
	uint8_t	 padding[4];		/* padding */
} stress_perf_stat_t;

/* per stressor perf info */
typedef struct {
	stress_perf_stat_t perf_stat[STRESS_PERF_MAX]; /* perf counters */
	int perf_opened;		/* count of opened counters */
	uint8_t	padding[4];		/* padding */
} stress_perf_t;

extern int stress_perf_open(stress_perf_t *sp);
extern int stress_perf_enable(stress_perf_t *sp);
extern int stress_perf_disable(stress_perf_t *sp);
extern int stress_perf_close(stress_perf_t *sp);
extern void stress_perf_stat_dump(FILE *yaml, stress_stressor_t *procs_head,
	const double duration);
extern void stress_perf_init(void);
#endif

#endif
