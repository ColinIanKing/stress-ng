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
#ifndef CORE_SCHED_H
#define CORE_SCHED_H

#include "core-attribute.h"

#if defined(HAVE_LINUX_SCHED_H)
#include <linux/sched.h>
#endif

#if defined(HAVE_SYSCALL_H)
#include <sys/syscall.h>
#endif

#if defined(__NR_sched_getattr)
#define HAVE_SCHED_GETATTR
#endif

#if defined(__NR_sched_setattr)
#define HAVE_SCHED_SETATTR
#endif

#if defined(__linux__) && 	\
    !defined(SCHED_EXT)
#define SCHED_EXT	(7)
#endif

typedef struct {
	const int sched;
	const char *const sched_name;
} stress_sched_types_t;

extern const stress_sched_types_t stress_sched_types[];
extern const size_t stress_sched_types_length;

extern const char *stress_get_sched_name(const int sched) RETURNS_NONNULL;
extern WARN_UNUSED int stress_set_sched(const pid_t pid, const int sched,
	const int sched_priority, const bool quiet);
extern WARN_UNUSED int32_t stress_get_opt_sched(const char *const str);
extern int sched_settings_apply(const bool quiet);
extern ssize_t sched_get_sched_ext_ops(char *buf, const size_t len);

#endif
