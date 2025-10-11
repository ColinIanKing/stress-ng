/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_TIME_H
#define CORE_TIME_H

#include "core-attribute.h"

extern double stress_timeval_to_double(const struct timeval *tv);
extern double stress_time_now(void);
extern const char *stress_duration_to_str(const double duration,
	const bool int_secs, const bool report_secs) RETURNS_NONNULL;

#endif
