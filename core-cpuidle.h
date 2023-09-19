/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#ifndef CORE_CPUIDLE_H
#define CORE_CPUIDLE_H

typedef struct cpu_cstate {
	struct cpu_cstate *next;
	uint32_t residency;	/* residency in microseconds */
	char	*cstate;	/* cstate name */
} cpu_cstate_t;

extern void stress_cpuidle_init(void);
extern void stress_cpuidle_free(void);
extern void stress_cpuidle_log_info(void);
extern cpu_cstate_t *stress_cpuidle_cstate_list_head(void);

#endif
