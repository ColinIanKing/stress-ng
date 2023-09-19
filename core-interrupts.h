/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#ifndef CORE_INTERRUPTS_H
#define CORE_INTERRUPTS_H

extern void stress_interrupts_start(stress_interrupts_t *counters);
extern void stress_interrupts_stop(stress_interrupts_t *counters);
extern void stress_interrupts_check_failure(const char *name,
	stress_interrupts_t *counters, uint32_t instance, int *rc);
extern void stress_interrupts_dump(FILE *yaml, stress_stressor_t *stressors_list);

#endif
