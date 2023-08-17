/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_PERF_H
#define CORE_PERF_H

/* Perf statistics */
#if defined(STRESS_PERF_STATS)
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
