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
#ifndef CORE_STACK_H
#define CORE_STACK_H

#include "stress-ng.h"

#define STRESS_SIGSTKSZ		(stress_get_sig_stack_size())
#define STRESS_MINSIGSTKSZ	(stress_get_min_sig_stack_size())

static inline WARN_UNUSED CONST ALWAYS_INLINE void *stress_align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern WARN_UNUSED void *stress_get_stack_top(void *start, const size_t size);
extern WARN_UNUSED int stress_sigaltstack_no_check(void *stack, const size_t size);
extern WARN_UNUSED int stress_sigaltstack(void *stack, const size_t size);
extern void stress_sigaltstack_disable(void);
extern WARN_UNUSED size_t stress_get_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_pthread_stack_size(void);
extern void stress_set_stack_smash_check_flag(const bool flag);
extern void stress_backtrace(void);

#endif
