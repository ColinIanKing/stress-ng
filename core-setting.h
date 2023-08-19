/*
 * Copyright (C) 2023      Colin Ian King.
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
#ifndef CORE_SETTING_H
#define CORE_SETTING_H

/* native setting types */
typedef enum {
	TYPE_ID_UNDEFINED,		/* no-id */
	TYPE_ID_UINT8,			/* uint8_t */
	TYPE_ID_INT8,			/* int8_t */
	TYPE_ID_UINT16,			/* uint16_t */
	TYPE_ID_INT16,			/* int16_t */
	TYPE_ID_UINT32,			/* uint32_t */
	TYPE_ID_INT32,			/* int32_t */
	TYPE_ID_UINT64,			/* uint64_t */
	TYPE_ID_INT64,			/* int64_t */
	TYPE_ID_SIZE_T,			/* size_t */
	TYPE_ID_SSIZE_T,		/* ssize_t */
	TYPE_ID_UINT,			/* unsigned int */
	TYPE_ID_INT,			/* signed int */
	TYPE_ID_ULONG,			/* unsigned long */
	TYPE_ID_LONG,			/* signed long */
	TYPE_ID_OFF_T,			/* off_t */
	TYPE_ID_STR,			/* char * */
	TYPE_ID_BOOL,			/* bool */
} stress_type_id_t;

extern void stress_settings_free(void);
extern void stress_settings_show(void);
extern int stress_set_setting(const char *name,
	const stress_type_id_t type_id, const void *value);
extern int stress_set_setting_global(const char *name,
	const stress_type_id_t type_id, const void *value);
extern bool stress_get_setting(const char *name, void *value);
extern int stress_set_setting_true(const char *name, const char *opt);

#endif
