/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#ifndef CORE_SHARED_HEAP_H
#define CORE_SHARED_HEAP_H

#include "stress-ng.h"

extern WARN_UNUSED void *stress_shared_heap_init(void);
extern void stress_shared_heap_deinit(void);
extern WARN_UNUSED void *stress_shared_heap_malloc(const size_t size);
extern WARN_UNUSED char *stress_shared_heap_dup_const(const char *str);

#endif
