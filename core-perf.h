/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_PERF_H
#define CORE_PERF_H

#include "stress-ng.h"

/* perf related constants */
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H) &&	\
    defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
#define STRESS_PERF_MAX		(128 + 16)

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
extern bool stress_perf_stat_succeeded(const stress_perf_t *sp);
extern void stress_perf_stat_dump(FILE *yaml, stress_stressor_t *procs_head,
	const double duration);
extern void stress_perf_init(void);
#endif

#endif
