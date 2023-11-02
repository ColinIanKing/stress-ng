/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_OUT_OF_MEMORY_H
#define CORE_OUT_OF_MEMORY_H

#include "stress-ng.h"

typedef int stress_oomable_child_func_t(const stress_args_t *args, void *context);

extern bool stress_process_oomed(const pid_t pid);
extern void stress_set_oom_adjustment(const stress_args_t *args, const bool killable);
extern int stress_oomable_child(const stress_args_t *args, void *context,
	stress_oomable_child_func_t func, const int flag);

#endif
