/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_THERMAL_ZONES_H
#define CORE_THERMAL_ZONES_H

/* Thermal Zones */
#if defined(STRESS_THERMAL_ZONES)
extern int stress_tz_init(stress_tz_info_t **tz_info_list);
extern void stress_tz_free(stress_tz_info_t **tz_info_list);
extern int stress_tz_get_temperatures(stress_tz_info_t **tz_info_list,
	stress_tz_t *tz);
extern void stress_tz_dump(FILE *yaml, stress_stressor_t *stressors_list);
#endif

#endif
