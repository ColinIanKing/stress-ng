/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_TIME_H
#define CORE_TIME_H

extern double stress_timeval_to_double(const struct timeval *tv);
extern double stress_timespec_to_double(const struct timespec *ts);
extern double stress_time_now(void);
extern const char *stress_duration_to_str(const double duration, const bool int_secs);

#endif
