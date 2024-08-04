/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "stress-ng.h"
#include "core-setting.h"

static stress_setting_t *setting_head;	/* setting list head */
static stress_setting_t *setting_tail;	/* setting list tail */

/*
 *  stress_settings_free()
 *	free the saved settings
 */
void stress_settings_free(void)
{
	stress_setting_t *setting = setting_head;

	while (setting) {
		stress_setting_t *next = setting->next;

		if (setting->type_id == TYPE_ID_STR)
			free(setting->u.str);
		free(setting->name);
		free(setting);
		setting = next;
	}
	setting_head = NULL;
	setting_tail = NULL;
}

static void stress_settings_show_setting(const stress_setting_t *setting)
{
	switch (setting->type_id) {
	case TYPE_ID_UINT8:
		pr_inf(" %-20.20s %" PRIu8 " (uint8_t)\n", setting->name, setting->u.uint8);
		break;
	case TYPE_ID_INT8:
		pr_inf(" %-20.20s %" PRId8 " (int8_t)\n", setting->name, setting->u.int8);
		break;
	case TYPE_ID_UINT16:
		pr_inf(" %-20.20s %" PRIu16 " (uint16_t)\n", setting->name, setting->u.uint16);
		break;
	case TYPE_ID_INT16:
		pr_inf(" %-20.20s %" PRId16 " (int16_t)\n", setting->name, setting->u.int16);
		break;
	case TYPE_ID_UINT32:
		pr_inf(" %-20.20s %" PRIu32 " (uint32_t)\n", setting->name, setting->u.uint32);
		break;
	case TYPE_ID_INT32:
		pr_inf(" %-20.20s %" PRId32 " (int32_t)\n", setting->name, setting->u.int32);
		break;
	case TYPE_ID_UINT64:
	case TYPE_ID_UINT64_BYTES:
		pr_inf(" %-20.20s %" PRIu64 " (uint64_t)\n", setting->name, setting->u.uint64);
		break;
	case TYPE_ID_INT64:
		pr_inf(" %-20.20s %" PRId64 " (int64_t)\n", setting->name, setting->u.int64);
		break;
	case TYPE_ID_SIZE_T:
	case TYPE_ID_SIZE_T_BYTES:
	case TYPE_ID_SIZE_T_METHOD:
		pr_inf(" %-20.20s %zu (size_t)\n", setting->name, setting->u.size);
		break;
	case TYPE_ID_SSIZE_T:
		pr_inf(" %-20.20s %zd (ssize_t)\n", setting->name, setting->u.ssize);
		break;
	case TYPE_ID_UINT:
		pr_inf(" %-20.20s %u (unsigned int)\n", setting->name, setting->u.uint);
		break;
	case TYPE_ID_INT:
	case TYPE_ID_INT_DOMAIN:
	case TYPE_ID_INT_PORT:
		pr_inf(" %-20.20s %d (signed int)\n", setting->name, setting->u.sint);
		break;
	case TYPE_ID_OFF_T:
		pr_inf(" %-20.20s %ju (off_t)\n", setting->name, (uintmax_t)setting->u.off);
		break;
	case TYPE_ID_STR:
		pr_inf(" %-20.20s %s (string)\n", setting->name, setting->u.str);
		break;
	case TYPE_ID_BOOL:
		pr_inf(" %-20.20s %u (boolean)\n", setting->name, setting->u.boolean);
		break;
	case TYPE_ID_UNDEFINED:
	default:
		pr_inf(" %-20.20s (unknown type)\n", setting->name);
		break;
	}
}

static int stress_setting_cmp(const void *p1, const void *p2)
{
	const stress_setting_t *s1 = *(stress_setting_t * const *)p1;
	const stress_setting_t *s2 = *(stress_setting_t * const *)p2;

	return strcmp(s1->name, s2->name);
}

void stress_settings_show(void)
{
	stress_setting_t *setting;
	stress_setting_t **settings;
	size_t i, n;

	if (!(g_opt_flags & OPT_FLAGS_SETTINGS))
		return;

	pr_inf("stress-ng settings:\n");
	for (n = 0, setting = setting_head; setting; setting = setting->next)
		n++;

	settings = calloc(n, sizeof(*settings));
	if (!settings)
		return;

	for (i = 0, setting = setting_head; setting; setting = setting->next, i++)
		settings[i] = setting;

	qsort(settings, n, sizeof(*settings), stress_setting_cmp);

	for (i = 0; i < n; i++)
		stress_settings_show_setting(settings[i]);
	free(settings);
}

/*
 *  stress_set_setting_generic()
 *	set a new setting
 */
static int stress_set_setting_generic(
	const char *name,
	const stress_type_id_t type_id,
	const void *value,
	const bool global)
{
	stress_setting_t *setting;

	if (!value) {
		(void)fprintf(stderr, "invalid setting '%s' value address (null)\n", name);
		_exit(EXIT_NOT_SUCCESS);
	}
	setting = calloc(1, sizeof *setting);
	if (!setting)
		goto err;

	setting->name = strdup(name);
	setting->proc = g_stressor_current;
	setting->type_id = type_id;
	setting->global = global;
	if (!setting->name) {
		free(setting);
		goto err;
	}

	switch (type_id) {
	case TYPE_ID_UINT8:
		setting->u.uint8 = *(const uint8_t *)value;
		break;
	case TYPE_ID_INT8:
		setting->u.int8 = *(const int8_t *)value;
		break;
	case TYPE_ID_UINT16:
		setting->u.uint16 = *(const uint16_t *)value;
		break;
	case TYPE_ID_INT16:
		setting->u.int16 = *(const int16_t *)value;
		break;
	case TYPE_ID_UINT32:
		setting->u.uint32 = *(const uint32_t *)value;
		break;
	case TYPE_ID_INT32:
		setting->u.int32 = *(const int32_t *)value;
		break;
	case TYPE_ID_UINT64:
	case TYPE_ID_UINT64_BYTES:
		setting->u.uint64 = *(const uint64_t *)value;
		break;
	case TYPE_ID_INT64:
		setting->u.int64 = *(const int64_t *)value;
		break;
	case TYPE_ID_SIZE_T:
	case TYPE_ID_SIZE_T_BYTES:
	case TYPE_ID_SIZE_T_METHOD:
		setting->u.size = *(const size_t *)value;
		break;
	case TYPE_ID_SSIZE_T:
		setting->u.ssize = *(const ssize_t *)value;
		break;
	case TYPE_ID_UINT:
		setting->u.uint = *(const unsigned int *)value;
		break;
	case TYPE_ID_INT:
	case TYPE_ID_INT_DOMAIN:
	case TYPE_ID_INT_PORT:
		setting->u.sint = *(const int *)value;
		break;
	case TYPE_ID_OFF_T:
		setting->u.off = *(const long *)value;
		break;
	case TYPE_ID_STR:
		setting->u.str = stress_const_optdup(value);
		if (!setting->u.str) {
			free(setting->name);
			free(setting);
			goto err;
		}
		break;
	case TYPE_ID_BOOL:
		setting->u.boolean = *(const bool *)value;
		break;
	case TYPE_ID_UNDEFINED:
	default:
		break;
	}
#if defined(DEBUG_SETTINGS)
	stress_settings_show_setting(setting);
#endif

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
	return 0;
}

/*
 *  stress_set_setting()
 *	set a new setting
 */
int stress_set_setting(
	const char *name,
	const stress_type_id_t type_id,
	const void *value)
{
	return stress_set_setting_generic(name, type_id, value, false);
}

/*
 *  stress_set_setting_global()
 *	set a new global setting;
 */
int stress_set_setting_global(
	const char *name,
	const stress_type_id_t type_id,
	const void *value)
{
	return stress_set_setting_generic(name, type_id, value, true);
}


/*
 *  stress_get_setting()
 *	get an existing setting
 */
bool stress_get_setting(const char *name, void *value)
{
	stress_setting_t *setting;
	bool set = false;
	bool found = false;

	for (setting = setting_head; setting; setting = setting->next) {
		if (setting->proc == g_stressor_current)
			found = true;
		if (found && ((setting->proc != g_stressor_current) && (!setting->global)))
			break;

		if (!strcmp(setting->name, name)) {
			switch (setting->type_id) {
			case TYPE_ID_UINT8:
				set = true;
				*(uint8_t *)value = setting->u.uint8;
				break;
			case TYPE_ID_INT8:
				set = true;
				*(int8_t *)value = setting->u.int8;
				break;
			case TYPE_ID_UINT16:
				set = true;
				*(uint16_t *)value = setting->u.uint16;
				break;
			case TYPE_ID_INT16:
				set = true;
				*(int16_t *)value = setting->u.int16;
				break;
			case TYPE_ID_UINT32:
				set = true;
				*(uint32_t *)value = setting->u.uint32;
				break;
			case TYPE_ID_INT32:
				set = true;
				*(int32_t *)value = setting->u.int32;
				break;
			case TYPE_ID_UINT64:
			case TYPE_ID_UINT64_BYTES:
				set = true;
				*(uint64_t *)value = setting->u.uint64;
				break;
			case TYPE_ID_INT64:
				set = true;
				*(int64_t *)value = setting->u.int64;
				break;
			case TYPE_ID_SIZE_T:
			case TYPE_ID_SIZE_T_BYTES:
			case TYPE_ID_SIZE_T_METHOD:
				set = true;
				*(size_t *)value = setting->u.size;
				break;
			case TYPE_ID_SSIZE_T:
				set = true;
				*(ssize_t *)value = setting->u.ssize;
				break;
			case TYPE_ID_UINT:
				set = true;
				*(unsigned int *)value = setting->u.uint;
				break;
			case TYPE_ID_INT:
			case TYPE_ID_INT_DOMAIN:
			case TYPE_ID_INT_PORT:
				set = true;
				*(int *)value = setting->u.sint;
				break;
			case TYPE_ID_OFF_T:
				set = true;
				*(long  *)value = setting->u.off;
				break;
			case TYPE_ID_STR:
				set = true;
				*(const char **)value = setting->u.str;
				break;
			case TYPE_ID_BOOL:
				set = true;
				*(bool *)value = setting->u.boolean;
				break;
			case TYPE_ID_UNDEFINED:
			default:
				set = true;
				break;
			}
#if defined(DEBUG_SETTINGS)
			stress_settings_show_setting(setting);
#endif
		}
	}
	return set;
}

/*
 *  stress_set_setting_true()
 *	create a setting of name name to true, ignore opt
 */
int stress_set_setting_true(const char *name, const char *opt)
{
        bool val = true;

        (void)opt;
        return stress_set_setting(name, TYPE_ID_BOOL, &val);
}
