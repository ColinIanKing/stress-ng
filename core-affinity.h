/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_AFFINITY_H
#define CORE_AFFINITY_H

extern int stress_set_cpu_affinity(const char *arg);
extern int stress_change_cpu(const stress_args_t *args, const int old_cpu);

#endif
