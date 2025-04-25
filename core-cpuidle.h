/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#ifndef CORE_CPUIDLE_H
#define CORE_CPUIDLE_H

typedef struct cpu_cstate {
	struct cpu_cstate *next;	/* next cpu c-state */
	uint32_t residency;		/* residency in microseconds */
	char	*cstate;		/* C-state name */
} cpu_cstate_t;

extern void stress_cpuidle_init(void);
extern void stress_cpuidle_free(void);
extern void stress_cpuidle_log_info(void);
extern cpu_cstate_t *stress_cpuidle_cstate_list_head(void);

extern void stress_cpuidle_read_cstates_begin(stress_cstate_stats_t *cstate_stats);
extern void stress_cpuidle_read_cstates_end(stress_cstate_stats_t *cstate_stats);
extern void stress_cpuidle_dump(FILE *yaml, stress_stressor_t *stressors_list);

#endif
