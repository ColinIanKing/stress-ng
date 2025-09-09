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
#include "stress-ng.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-capabilities.h"

static const stress_help_t help[] = {
	{ NULL,	"led N",	"start N workers that read and set LED settings" },
	{ NULL,	"led-ops N",	"stop after N LED bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

#define MAX_BUF_SIZE			(8192)
#define STRESS_LED_MAX_BRIGHTNESS	(65535)

typedef struct stress_led_info {
	char *path;				/* LED /sysfs path name */
	char *name;				/* LED dev name */
	char *orig_trigger;			/* LED original default trigger setting */
	char *trigger;				/* LED trigger settings */
	int orig_brightness;			/* LED original brightness setting */
	int max_brightness;			/* LED maximum brightness setting */
	struct stress_led_info	*next;		/* next in list */
} stress_led_info_t;

/*
 *  stress_led_dot_filter()
 *	scandir filter out dot filenames
 */
static int CONST stress_led_dot_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.')
		return 0;
	return 1;
}

static char *stress_led_orig_trigger(const char *str)
{
	const char *start, *end;
	char *orig;
	size_t len;

	start = strchr(str, '[');
	if (!start)
		return NULL;
	start++;
	end = strchr(start, ']');
	if (!end)
		return NULL;
	len = 1 + (end - start);
	if (len < 2)
		return NULL;
	orig = (char *)calloc(len, sizeof(*orig));
	if (!orig)
		return NULL;
	(void)shim_strscpy(orig, start, len);
	return orig;
}

static void stress_led_brightness(const char *path, const int brightness)
{
	char filename[PATH_MAX];
	char val[16];

	(void)snprintf(filename, sizeof(filename), "%s/brightness", path);
	(void)snprintf(val, sizeof(val), "%d\n", brightness);
	stress_system_write(filename, val, strlen(val));
}

static void stress_led_trigger(const char *path, const char *trigger)
{
	char filename[PATH_MAX];

	(void)snprintf(filename, sizeof(filename), "%s/trigger", path);
	stress_system_write(filename, trigger, strlen(trigger));
}

/*
 *  stress_led_info_free()
 *	free list of stress_led_info_t items
 */
static void stress_led_info_free(stress_led_info_t *led_info_list)
{
	stress_led_info_t *led_info = led_info_list;

	while (led_info) {
		stress_led_info_t *next = led_info->next;

		stress_led_brightness(led_info->path, led_info->orig_brightness);
		stress_led_trigger(led_info->path, led_info->orig_trigger);

		free(led_info->path);
		free(led_info->name);
		free(led_info->trigger);
		free(led_info->orig_trigger);
		free(led_info);
		led_info = next;
	}
}

/*
 *  stress_led_info_get()
 *	get a list of LED device paths of stress_led_info items
 */
static stress_led_info_t *stress_led_info_get(void)
{
	static const char sys_devices[] = "/sys/class/leds";
	stress_led_info_t *led_info_list = NULL;

	int n_devs, i;
	struct dirent **led_list = NULL;

	n_devs = scandir(sys_devices, &led_list, stress_led_dot_filter, NULL);
	for (i = 0; i < n_devs; i++) {
		const int j = stress_mwc16modn(n_devs);
		struct dirent *tmp;

		tmp = led_list[j];
		led_list[j] = led_list[i];
		led_list[i] = tmp;
	}

	for (i = 0; i < n_devs; i++) {
		stress_led_info_t *led_info;

		led_info = (stress_led_info_t *)calloc(1, sizeof(*led_info));
		if (led_info) {
			char led_path[PATH_MAX];
			char buf[MAX_BUF_SIZE];
			int fd;
			ssize_t len;

			(void)snprintf(led_path, sizeof(led_path),
				"%s/%s", sys_devices, led_list[i]->d_name);
			led_info->path = shim_strdup(led_path);
			if (!led_info->path)
				goto led_free_info;

			led_info->name = shim_strdup(led_list[i]->d_name);
			if (!led_info->name)
				goto led_free_path;

			(void)snprintf(led_path, sizeof(led_path),
				"%s/%s/trigger", sys_devices, led_list[i]->d_name);
			fd = open(led_path, O_RDONLY);
			if (fd < 0)
				goto led_free_name;
			(void)shim_memset(buf, 0, sizeof(buf));
			len = stress_read_buffer(fd, buf, sizeof(buf), true);
			(void)close(fd);
			if (len < 0)
				goto led_free_name;
			led_info->trigger = shim_strdup(buf);
			if (!led_info->trigger)
				goto led_free_name;

			led_info->orig_trigger = stress_led_orig_trigger(buf);
			if (!led_info->orig_trigger)
				goto led_free_trigger;

			(void)snprintf(led_path, sizeof(led_path),
				"%s/%s/brightness", sys_devices, led_list[i]->d_name);
			if (stress_system_read(led_path, buf, sizeof(buf)) < 1)
				goto led_free_orig_trigger;
			led_info->orig_brightness = atoi(buf);

			(void)snprintf(led_path, sizeof(led_path),
				"%s/%s/max_brightness", sys_devices, led_list[i]->d_name);
			if (stress_system_read(led_path, buf, sizeof(buf)) < 1)
				goto led_free_orig_trigger;
			led_info->max_brightness = atoi(buf);
			/* Threshold values */
			if (led_info->max_brightness < 0)
				led_info->max_brightness = 0;
			if (led_info->max_brightness > STRESS_LED_MAX_BRIGHTNESS)
				led_info->max_brightness = STRESS_LED_MAX_BRIGHTNESS;

			led_info->next = led_info_list;
			led_info_list = led_info;
			continue;

led_free_orig_trigger:
			free(led_info->orig_trigger);
led_free_trigger:
			free(led_info->trigger);
led_free_name:
			free(led_info->name);
led_free_path:
			free(led_info->path);
led_free_info:
			free(led_info);
		}
	}
	stress_dirent_list_free(led_list, n_devs);

	return led_info_list;
}

/*
 *  stress_led_exercise()
 *	exercise all LED files in a given LED path
 */
static void stress_led_exercise(stress_args_t *args, stress_led_info_t *led_info)
{
	char buf[MAX_BUF_SIZE];
	char *ptr, *token;
	int brightness;

	(void)shim_strscpy(buf, led_info->trigger, sizeof(buf));
	for (ptr = buf; (token = strtok(ptr, " ")) != NULL; ptr = NULL) {
		char *tmp;
		int delta = 1;

		if (UNLIKELY(!stress_continue(args)))
			break;
		if (token[0] == '[')
			token++;
		tmp = strchr(token, ']');
		if (tmp)
			*tmp = '\0';
		stress_led_trigger(led_info->path, token);

		if (led_info->max_brightness > 16)
			delta = (led_info->max_brightness + 1) / 16;
		for (brightness = 0; brightness <= led_info->max_brightness; brightness += delta) {
			stress_led_brightness(led_info->path, brightness);
		}
	}
	stress_led_brightness(led_info->path, led_info->orig_brightness);
	stress_led_trigger(led_info->path, led_info->orig_trigger);
}

/*
 *  stress_led()
 *	stress /sysfs LED files with open/read/close and mmap where possible
 */
static int stress_led(stress_args_t *args)
{
	stress_led_info_t *led_info_list;
	stress_led_info_t *led_info;
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);

	if (!is_root && (stress_instance_zero(args)))
		pr_inf("%s: unable to set LED settings, need root privilege\n", args->name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	led_info_list = stress_led_info_get();
	if (!led_info_list) {
		pr_inf_skip("%s: no LED sysfs entries found, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	do {
		for (led_info = led_info_list; led_info; led_info = led_info->next) {
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_led_exercise(args, led_info);
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_led_info_free(led_info_list);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_led_info = {
	.stressor = stress_led,
	.classifier = CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_led_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
