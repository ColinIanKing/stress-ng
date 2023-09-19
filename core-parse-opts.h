/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_PARSE_OPTS_H
#define CORE_PARSE_OPTS_H

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} stress_scale_t;

extern void stress_check_max_stressors(const char *const msg, const int val);
extern void stress_check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void stress_check_range_bytes(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern WARN_UNUSED uint32_t stress_get_uint32(const char *const str);
extern WARN_UNUSED int32_t stress_get_int32(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_scale(const char *const str,
	const stress_scale_t scales[], const char *const msg);
extern WARN_UNUSED uint64_t stress_get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_percent(const char *const str,
	const uint32_t instances, const uint64_t max, const char *const errmsg);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_memory(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_filesystem(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_time(const char *const str);
extern void stress_check_power_of_2(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);

#endif
