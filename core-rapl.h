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
#ifndef CORE_RAPL_H
#define CORE_RAPL_H

#include "core-arch.h"

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)

#define STRESS_RAPL
#define STRESS_RAPL_DOMAINS_MAX		(32)

#include "stress-ng.h"

#define STRESS_RAPL_DATA_RAPLSTAT	(0)
#define STRESS_RAPL_DATA_STRESSOR	(1)
#define STRESS_RAPL_DATA_MAX		(STRESS_RAPL_DATA_STRESSOR + 1)

typedef struct {
	double energy_uj;		/* Previous energy reading in micro Joules */
	double time;			/* Time of previous reading */
	double power_watts;		/* Computed power based on time and energy */
} stress_rapl_data_t;

/* RAPL domain info */
typedef struct stress_rapl_domain {
	struct stress_rapl_domain *next;/* Next RAPL domain */
	size_t index;			/* RAPL index to RAPL array */
	char *name;			/* RAPL name */
	char *domain_name;		/* RAPL domain name */
	double max_energy_uj;		/* Max energy in micro Joules */
	stress_rapl_data_t data[STRESS_RAPL_DATA_MAX];
} stress_rapl_domain_t;

typedef struct {
	double read_time;
        double power_watts[STRESS_RAPL_DOMAINS_MAX];
} stress_rapl_t;

extern void stress_rapl_free_domains(stress_rapl_domain_t *rapl_domains);
extern int stress_rapl_get_domains(stress_rapl_domain_t **rapl_domains);
extern int stress_rapl_get_power_raplstat(stress_rapl_domain_t *rapl_domains);
extern int stress_rapl_get_power_stressor(stress_rapl_domain_t *rapl_domains, stress_rapl_t *rapl);
extern void stress_rapl_dump(FILE *yaml, stress_stressor_t *stressors_list, stress_rapl_domain_t *rapl_domains);
#endif

#endif
