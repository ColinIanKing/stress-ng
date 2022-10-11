/*
 * Copyright (C)      2022 Colin Ian King
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
