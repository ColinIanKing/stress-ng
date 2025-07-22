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
#ifndef CORE_PARSE_OPTS_H
#define CORE_PARSE_OPTS_H

#include "core-attribute.h"
#include "core-setting.h"

typedef struct {
	const int opt;			/* optarg option */
	const char *opt_name;		/* optarg name */
	stress_type_id_t type_id;	/* option setting type */
	uint64_t min;			/* option min */
	uint64_t max;			/* option max */
	void *data;			/* option specific data */
} stress_opt_t;

#define END_OPT { 0, NULL, TYPE_ID_UNDEFINED, (uint64_t)0, (uint64_t)0, NULL }

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} stress_scale_t;

extern void stress_check_max_stressors(const char *const msg, const int val);
extern void stress_check_signed_range(const char *const opt, const int64_t val,
	const int64_t lo, const int64_t hi);
extern void stress_check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void stress_check_range_bytes(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);

extern WARN_UNUSED int stress_get_int(const char *const str);
extern WARN_UNUSED int8_t stress_get_int8(const char *const str);
extern WARN_UNUSED int16_t stress_get_int16(const char *const str);
extern WARN_UNUSED int32_t stress_get_int32(const char *const str);
extern WARN_UNUSED int64_t stress_get_int64(const char *const str);

extern WARN_UNUSED unsigned int stress_get_uint(const char *const str);
extern WARN_UNUSED uint8_t stress_get_uint8(const char *const str);
extern WARN_UNUSED uint16_t stress_get_uint16(const char *const str);
extern WARN_UNUSED uint32_t stress_get_uint32(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64(const char *const str);

extern WARN_UNUSED uint64_t stress_get_uint64_scale(const char *const str,
	const stress_scale_t scales[], const char *const msg);

extern WARN_UNUSED int32_t stress_get_int32_instance_percent(const char *const str);

extern WARN_UNUSED uint64_t stress_get_uint64_byte_scale(const char *const str);

extern WARN_UNUSED uint64_t stress_get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_percent(const char *const str,
	const uint32_t instances, const uint64_t max, bool *percentage, const char *const errmsg);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_memory(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_time(const char *const str);
extern WARN_UNUSED int stress_parse_opt(const char *stressor_name, const char *optarg, const stress_opt_t *opt);
extern WARN_UNUSED const char *stress_unimplemented_method(const size_t i);

#endif
