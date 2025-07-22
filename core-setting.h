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
#ifndef CORE_SETTING_H
#define CORE_SETTING_H

/* native setting types */
typedef enum {
	TYPE_ID_UNDEFINED = 0,		/* no-id */
	TYPE_ID_UINT8,			/* uint8_t */
	TYPE_ID_INT8,			/* int8_t */
	TYPE_ID_UINT16,			/* uint16_t */
	TYPE_ID_INT16,			/* int16_t */
	TYPE_ID_UINT32,			/* uint32_t */
	TYPE_ID_INT32,			/* int32_t */
	TYPE_ID_UINT64,			/* uint64_t */
	TYPE_ID_UINT64_BYTES_FS_PERCENT,/* uint64_t in % units, file system */
	TYPE_ID_UINT64_BYTES_FS,	/* uint64_t in bytes units, file system */
	TYPE_ID_UINT64_BYTES_VM,	/* uint64_t in bytes units, memory */
	TYPE_ID_INT64,			/* int64_t */
	TYPE_ID_SIZE_T,			/* size_t */
	TYPE_ID_SIZE_T_BYTES_FS_PERCENT,/* size_t in % units, file system */
	TYPE_ID_SIZE_T_BYTES_FS,	/* size_t in bytes units, file system */
	TYPE_ID_SIZE_T_BYTES_VM,	/* size_t in bytes units, memory */
	TYPE_ID_SSIZE_T,		/* ssize_t */
	TYPE_ID_UINT,			/* unsigned int */
	TYPE_ID_INT,			/* signed int */
	TYPE_ID_INT_DOMAIN,		/* net domain */
	TYPE_ID_INT_PORT,		/* net port */
	TYPE_ID_OFF_T,			/* off_t is always in bytes units */
	TYPE_ID_STR,			/* char * */
	TYPE_ID_BOOL,			/* bool */
	TYPE_ID_SIZE_T_METHOD,		/* method index */
	TYPE_ID_CALLBACK,		/* callback function */
} stress_type_id_t;

/* settings for storing opt arg parsed data */
typedef struct stress_setting {
	struct stress_setting *next;	/* next setting in list */
	struct stress_stressor_info *proc;
	const char *stressor_name;	/* name of stressor */
	const char *name;		/* name of setting */
	const void *opt;		/* optional opt pointer */
	stress_type_id_t type_id;	/* setting type */
	bool		global;		/* true if global */
	union {				/* setting value */
		uint8_t		uint8;	/* TYPE_ID_UINT8 */
		int8_t		int8;	/* TYPE_ID_INT8 */
		uint16_t	uint16;	/* TYPE_ID_UINT16 */
		int16_t		int16;	/* TYPE_ID_INT16 */
		uint32_t	uint32;	/* TYPE_ID_UINT32 */
		int32_t		int32;	/* TYPE_ID_INT32 */
		uint64_t	uint64;	/* TYPE_ID_UINT64 */
		int64_t		int64;	/* TYPE_ID_INT64 */
		size_t		size;	/* TYPE_ID_SIZE_T, TYPE_ID_SIZE_T_METHOD */
		ssize_t		ssize;	/* TYPE_ID_SSIZE_T */
		unsigned int	uint;	/* TYPE_ID_UINT */
		signed int	sint;	/* TYPE_ID_INT, TYPE_ID_INT_DOMAIN, TYPE_ID_INT_PORT */
		off_t		off;	/* TYPE_ID_OFF_T */
		char		*str;	/* TYPE_ID_STR */
		bool		boolean;/* TYPE_ID_BOOL */
	} u;
} stress_setting_t;

extern void stress_settings_free(void);
extern void stress_settings_show(void);
extern int stress_set_setting(const char *stressor_name, const char *name,
	const stress_type_id_t type_id, const void *value);
extern int stress_set_setting_global(const char *name,
	const stress_type_id_t type_id, const void *value);
extern bool stress_get_setting(const char *name, void *value);
extern int stress_set_setting_true(const char *stressor_name, const char *name,
	const char *opt);
extern void stress_settings_dbg(stress_args_t *args);

#endif
