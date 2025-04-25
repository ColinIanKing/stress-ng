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
#ifndef CORE_OUT_OF_MEMORY_H
#define CORE_OUT_OF_MEMORY_H

#include "stress-ng.h"

typedef int stress_oomable_child_func_t(stress_args_t *args, void *context);

extern bool stress_process_oomed(const pid_t pid);
extern void stress_set_oom_adjustment(stress_args_t *args, const bool killable);
extern int stress_oomable_child(stress_args_t *args, void *context,
	stress_oomable_child_func_t func, const int flag);

#endif
