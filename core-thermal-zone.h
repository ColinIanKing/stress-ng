/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_THERMAL_ZONES_H
#define CORE_THERMAL_ZONES_H

/* linux thermal zones */
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct stress_tz_info {
	char	*path;			/* thermal zone path */
	char 	*type;			/* thermal zone type */
	uint32_t type_instance;		/* thermal zone instance # */
	size_t	index;			/* thermal zone # index */
	struct stress_tz_info *next;	/* next thermal zone in list */
} stress_tz_info_t;

typedef struct {
	uint64_t temperature;		/* temperature in Celsius * 1000 */
} stress_tz_stat_t;

typedef struct {
	stress_tz_stat_t tz_stat[STRESS_THERMAL_ZONES_MAX];
} stress_tz_t;

extern int stress_tz_init(stress_tz_info_t **tz_info_list);
extern void stress_tz_free(stress_tz_info_t **tz_info_list);
extern int stress_tz_get_temperatures(stress_tz_info_t **tz_info_list,
	stress_tz_t *tz);
extern void stress_tz_dump(FILE *yaml, stress_stressor_t *stressors_list);
#endif

#endif
