/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_SCHED_H
#define CORE_SCHED_H

extern const char *stress_get_sched_name(const int sched);
extern WARN_UNUSED int stress_set_sched(const pid_t pid, const int sched,
	const int sched_priority, const bool quiet);
extern WARN_UNUSED int32_t stress_get_opt_sched(const char *const str);
extern int sched_settings_apply(const bool quiet);

#endif
