/*
 * Copyright (C) 2017-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static setting_t *setting_head;	/* setting list head */
static setting_t *setting_tail;	/* setting list tail */

#if defined(DEBUG_SETTINGS)
#define	DBG(...)	pr_inf(__VA_ARGS__)
#else
#define DBG(...)
#endif

/*
 *  free_settings()
 *	free the saved settings
 */
void free_settings(void)
{
	setting_t *setting = setting_head;

	while (setting) {
		setting_t *next = setting->next;

		free(setting->name);
		free(setting);
		setting = next;
	}
	setting_head = NULL;
	setting_tail = NULL;
}


/*
 *  set_setting_generic()
 *	set a new setting;
 */
static int set_setting_generic(
	const char *name,
	const type_id_t type_id,
	const void *value,
	const bool global)
{
	setting_t *setting;

	if (!value) {
		(void)fprintf(stderr, "invalid setting '%s' value address (null)\n", name);
		_exit(EXIT_NOT_SUCCESS);
	}
	setting = calloc(1, sizeof *setting);
	if (!setting)
		goto err;

	setting->name = strdup(name);
	setting->proc = g_proc_current;
	setting->type_id = type_id;
	setting->global = global;
	if (!setting->name) {
		free(setting);
		goto err;
	}

	DBG("%s: %s, global = %s\n", __func__, name, global ? "true" : "false");

	switch (type_id) {
	case TYPE_ID_UINT8:
		setting->u.uint8 = *(const uint8_t *)value;
		DBG("%s: UINT8: %s -> %" PRIu8 "\n", __func__, name, setting->u.uint8);
		break;
	case TYPE_ID_INT8:
		setting->u.int8 = *(const int8_t *)value;
		DBG("%s: INT8: %s -> %" PRId8 "\n", __func__, name, setting->u.int8);
		break;
	case TYPE_ID_UINT16:
		setting->u.uint16 = *(const uint16_t *)value;
		DBG("%s: UINT16: %s -> %" PRIu16 "\n", __func__, name, setting->u.uint16);
		break;
	case TYPE_ID_INT16:
		setting->u.int16 = *(const int16_t *)value;
		DBG("%s: INT16: %s -> %" PRId16 "\n", __func__, name, setting->u.int16);
		break;
	case TYPE_ID_UINT32:
		setting->u.uint32 = *(const uint32_t *)value;
		DBG("%s: UINT32: %s -> %" PRIu32 "\n", __func__, name, setting->u.uint32);
		break;
	case TYPE_ID_INT32:
		setting->u.int32 = *(const int32_t *)value;
		DBG("%s: INT32: %s -> %" PRId32 "\n", __func__, name, setting->u.int32);
		break;
	case TYPE_ID_UINT64:
		setting->u.uint64 = *(const uint64_t *)value;
		DBG("%s: UINT64: %s -> %" PRIu64 "\n", __func__, name, setting->u.uint64);
		break;
	case TYPE_ID_INT64:
		setting->u.int64 = *(const int64_t *)value;
		DBG("%s: INT64: %s -> %" PRId64 "\n", __func__, name, setting->u.int64);
		break;
	case TYPE_ID_SIZE_T:
		setting->u.size = *(const size_t *)value;
		DBG("%s: SIZE_T: %s -> %zu\n", __func__, name, setting->u.size);
		break;
	case TYPE_ID_SSIZE_T:
		setting->u.ssize = *(const ssize_t *)value;
		DBG("%s: SSIZE_T: %s -> %zd\n", __func__, name, setting->u.ssize);
		break;
	case TYPE_ID_UINT:
		setting->u.uint = *(const unsigned int *)value;
		DBG("%s: UINT: %s -> %u\n", __func__, name, setting->u.uint);
		break;
	case TYPE_ID_INT:
		setting->u.sint = *(const int *)value;
		DBG("%s: UINT: %s -> %d\n", __func__, name, setting->u.sint);
		break;
	case TYPE_ID_ULONG:
		setting->u.ulong = *(const unsigned long  *)value;
		DBG("%s: ULONG: %s -> %lu\n", __func__, name, setting->u.ulong);
		break;
	case TYPE_ID_LONG:
		setting->u.slong = *(const long  *)value;
		DBG("%s: LONG: %s -> %ld\n", __func__, name, setting->u.slong);
		break;
	case TYPE_ID_OFF_T:
		setting->u.off = *(const long *)value;
		DBG("%s: OFF_T: %s -> %lu\n", __func__, name, (unsigned long)setting->u.off);
		break;
	case TYPE_ID_STR:
		setting->u.str = (const char *)value;
		DBG("%s: STR: %s -> %s\n", __func__, name, setting->u.str);
		break;
	case TYPE_ID_BOOL:
		setting->u.boolean = *(const bool *)value;
		DBG("%s: BOOL: %s -> %d\n", __func__, name, setting->u.boolean);
		break;
	case TYPE_ID_UINTPTR_T:
		setting->u.uintptr = *(const uintptr_t *)value;
		DBG("%s: UINTPTR_R: %s -> %p\n", __func__, name, (void *)setting->u.uintptr);
		break;
	case TYPE_ID_UNDEFINED:
	default:
		DBG("%s: UNDEF: %s -> ?\n", __func__, name);
		break;
	}

	if (setting_tail) {
		setting_tail->next = setting;
	} else {
		setting_head = setting;
	}
	setting_tail = setting;

	return 0;
err:
	(void)fprintf(stderr, "cannot allocate setting '%s'\n", name);
	_exit(EXIT_NO_RESOURCE);

	return 1;
}

/*
 *  set_setting()
 *	set a new setting;
 */
int set_setting(
	const char *name,
	const type_id_t type_id,
	const void *value)
{
	return set_setting_generic(name, type_id, value, false);
}

/*
 *  set_setting_global()
 *	set a new global setting;
 */
int set_setting_global(
	const char *name,
	const type_id_t type_id,
	const void *value)
{
	return set_setting_generic(name, type_id, value, true);
}


/*
 *  get_setting()
 *	get an existing setting;
 */
bool get_setting(const char *name, void *value)
{
	setting_t *setting;
	bool set = false;
	bool found = false;

	DBG("%s: get %s\n", __func__, name);

	for (setting = setting_head; setting; setting = setting->next) {
		if (setting->proc == g_proc_current)
			found = true;
		if (found && ((setting->proc != g_proc_current) && (!setting->global)))
			break;

		if (!strcmp(setting->name, name)) {
			switch (setting->type_id) {
			case TYPE_ID_UINT8:
				set = true;
				*(uint8_t *)value = setting->u.uint8;
				DBG("%s: UINT8: %s -> %" PRIu8 "\n", __func__, name, setting->u.uint8);
				break;
			case TYPE_ID_INT8:
				set = true;
				*(int8_t *)value = setting->u.int8;
				DBG("%s: INT8: %s -> %" PRId8 "\n", __func__, name, setting->u.int8);
				break;
			case TYPE_ID_UINT16:
				set = true;
				*(uint16_t *)value = setting->u.uint16;
				DBG("%s: UINT16: %s -> %" PRIu16 "\n", __func__, name, setting->u.uint16);
				break;
			case TYPE_ID_INT16:
				set = true;
				*(int16_t *)value = setting->u.int16;
				DBG("%s: INT16: %s -> %" PRId16 "\n", __func__, name, setting->u.int16);
				break;
			case TYPE_ID_UINT32:
				set = true;
				*(uint32_t *)value = setting->u.uint32;
				DBG("%s: UINT32: %s -> %" PRIu32 "\n", __func__, name, setting->u.uint32);
				break;
			case TYPE_ID_INT32:
				set = true;
				*(int32_t *)value = setting->u.int32;
				DBG("%s: INT32: %s -> %" PRId32 "\n", __func__, name, setting->u.int32);
				break;
			case TYPE_ID_UINT64:
				set = true;
				*(uint64_t *)value = setting->u.uint64;
				DBG("%s: UINT64: %s -> %" PRIu64 "\n", __func__, name, setting->u.uint64);
				break;
			case TYPE_ID_INT64:
				set = true;
				*(int64_t *)value = setting->u.int64;
				DBG("%s: INT64: %s -> %" PRId64 "\n", __func__, name, setting->u.int64);
				break;
			case TYPE_ID_SIZE_T:
				set = true;
				*(size_t *)value = setting->u.size;
				DBG("%s: SIZE_T: %s -> %zu\n", __func__, name, setting->u.size);
				break;
			case TYPE_ID_SSIZE_T:
				set = true;
				*(ssize_t *)value = setting->u.ssize;
				DBG("%s: SSIZE_T: %s -> %zd\n", __func__, name, setting->u.ssize);
				break;
			case TYPE_ID_UINT:
				set = true;
				*(unsigned int *)value = setting->u.uint;
				DBG("%s: UINT: %s -> %u\n", __func__, name, setting->u.uint);
				break;
			case TYPE_ID_INT:
				set = true;
				*(int *)value = setting->u.sint;
				DBG("%s: UINT: %s -> %d\n", __func__, name, setting->u.sint);
				break;
			case TYPE_ID_ULONG:
				set = true;
				*(unsigned long  *)value = setting->u.ulong;
				DBG("%s: ULONG: %s -> %lu\n", __func__, name, setting->u.ulong);
				break;
			case TYPE_ID_LONG:
				set = true;
				*(long *)value = setting->u.slong;
				DBG("%s: LONG: %s -> %ld\n", __func__, name, setting->u.slong);
				break;
			case TYPE_ID_OFF_T:
				set = true;
				*(long  *)value = setting->u.off;
				DBG("%s: OFF_T: %s -> %lu\n", __func__, name, (unsigned long)setting->u.off);
				break;
			case TYPE_ID_STR:
				set = true;
				*(const char **)value = setting->u.str;
				DBG("%s: STR: %s -> %s\n", __func__, name, setting->u.str);
				break;
			case TYPE_ID_BOOL:
				set = true;
				*(bool *)value = setting->u.boolean;
				DBG("%s: BOOL: %s -> %d\n", __func__, name, setting->u.boolean);
				break;
			case TYPE_ID_UINTPTR_T:
				set = true;
				*(uintptr_t *)value = setting->u.uintptr;
				DBG("%s: UINTPTR_T: %s -> %p\n", __func__, name, (void *)setting->u.uintptr);
				break;
			case TYPE_ID_UNDEFINED:
			default:
				set = true;
				DBG("%s: UNDEF: %s -> ?\n", __func__, name);
				break;
			}
		}
	}
	return set;
}
