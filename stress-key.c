/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"key N",	"start N workers exercising key operations" },
	{ NULL,	"key-ops N",	"stop after N key bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_KEYUTILS_H) && \
    defined(HAVE_ADD_KEY) && \
    defined(HAVE_KEYCTL)

#define MAX_KEYS 	(256)
#define KEYCTL_TIMEOUT	(2)

static long sys_keyctl(int cmd, ...)
{
	va_list args;
	long int arg0, arg1, arg2, ret;

	va_start(args, cmd);
	arg0 = va_arg(args, long int);
	arg1 = va_arg(args, long int);
	arg2 = va_arg(args, long int);
	ret = syscall(__NR_keyctl, cmd, arg0, arg1, arg2);
	va_end(args);

	return ret;
}

static key_serial_t sys_add_key(
	const char *type,
	const char *description,
	const void *payload,
	size_t plen,
	key_serial_t keyring)
{
	return (key_serial_t)syscall(__NR_add_key, type,
		description, payload, plen, keyring);
}

#if defined(HAVE_REQUEST_KEY)
static key_serial_t sys_request_key(
	const char *type,
	const char *description,
	const char *callout_info,
	key_serial_t keyring)
{
	return (key_serial_t)syscall(__NR_request_key,
		type, description, callout_info, keyring);
}
#endif

/*
 *  stress_key
 *	stress key operations
 */
static int stress_key(const args_t *args)
{
	key_serial_t keys[MAX_KEYS];
	pid_t ppid = getppid();
	int rc = EXIT_SUCCESS;
	bool timeout_supported = true;
	bool no_error = true;

	do {
		size_t i = 0, n = 0;
		char description[64];
		char payload[64];

		/* Add as many keys as we are allowed */
		for (n = 0; n < MAX_KEYS; n++) {
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, args->instance, n);
			(void)snprintf(payload, sizeof(payload),
				"somedata-%zu", n);

			keys[n] = sys_add_key("user", description,
				payload, strlen(payload),
				KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] < 0) {
				if (errno == ENOSYS) {
					pr_inf("%s: skipping stressor, add_key not implemented\n",
						args->name);
					no_error = false;
					rc = EXIT_NOT_IMPLEMENTED;
					goto tidy;
				}
				if ((errno == ENOMEM) || (errno == EDQUOT))
					break;
				pr_fail_err("add_key");
				goto tidy;
			}
#if defined(KEYCTL_SET_TIMEOUT)
			if (timeout_supported) {
				if (sys_keyctl(KEYCTL_SET_TIMEOUT, keys[n], KEYCTL_TIMEOUT) < 0) {
					/* Some platforms don't support this */
					if (errno == ENOSYS) {
						timeout_supported = false;
					} else {
						pr_fail_err("keyctl KEYCTL_SET_TIMEOUT");
					}
				}
			}
			if (!g_keep_stressing_flag)
				goto tidy;
#endif
		}

		/* And manipulate the keys */
		for (i = 0; i < n; i++) {
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, args->instance, i);
#if defined(KEYCTL_DESCRIBE)
			if (sys_keyctl(KEYCTL_DESCRIBE, keys[i], description) < 0)
				pr_fail_err("keyctl KEYCTL_DESCRIBE");
			if (!g_keep_stressing_flag)
				goto tidy;
#endif

			(void)snprintf(payload, sizeof(payload),
				"somedata-%zu", n);
#if defined(KEYCTL_UPDATE)
			if (sys_keyctl(KEYCTL_UPDATE, keys[i],
			    payload, strlen(payload)) < 0) {
				if ((errno != ENOMEM) && (errno != EDQUOT))
					pr_fail_err("keyctl KEYCTL_UPDATE");
			}
			if (!g_keep_stressing_flag)
				goto tidy;
#endif

#if defined(KEYCTL_READ)
			(void)memset(payload, 0, sizeof(payload));
			if (sys_keyctl(KEYCTL_READ, keys[i],
			    payload, sizeof(payload)) < 0)
				pr_fail_err("keyctl KEYCTL_READ");
			if (!g_keep_stressing_flag)
				goto tidy;
#endif

#if defined(HAVE_REQUEST_KEY)
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, args->instance, i);
			if (sys_request_key("user", description, NULL,
				KEY_SPEC_PROCESS_KEYRING) < 0) {
				pr_fail_err("request_key");
			}
			if (!g_keep_stressing_flag)
				goto tidy;
#endif


#if defined(KEYCTL_CHOWN)
			(void)sys_keyctl(KEYCTL_CHOWN, keys[i], getuid(), -1);
			(void)sys_keyctl(KEYCTL_CHOWN, keys[i], -1, getgid());
#endif

#if defined(KEYCTL_SETPERM)
			(void)sys_keyctl(KEYCTL_SETPERM, keys[i], KEY_USR_ALL);
#endif
#if defined(KEYCTL_REVOKE)
			if (mwc1())
				(void)sys_keyctl(KEYCTL_REVOKE, keys[i]);
#endif
#if defined(KEYCTL_INVALIDATE)
			(void)sys_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
		}
tidy:
		inc_counter(args);
		/* If we hit too many errors and bailed out early, clean up */
		for (i = 0; i < n; i++) {
			if (keys[i] >= 0) {
#if defined(KEYCTL_INVALIDATE)
				(void)sys_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
			}
		}
	} while (no_error && keep_stressing());

	return rc;
}

stressor_info_t stress_key_info = {
	.stressor = stress_key,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_key_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
