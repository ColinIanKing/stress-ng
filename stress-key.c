/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_KEY)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <keyutils.h>
#include <stdarg.h>

#define MAX_KEYS 	(256)

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

#if defined(__NR_request_key)
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
int stress_key(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	key_serial_t keys[MAX_KEYS];
	pid_t ppid = getppid();

	do {
		size_t i, n = 0;
		char description[64];
		char payload[64];

		/* Add as many keys as we are allowed */
		for (n = 0; n < MAX_KEYS; n++) {
			snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, instance, n);
			snprintf(payload, sizeof(payload),
				"somedata-%zu", n);

			keys[n] = sys_add_key("user", description,
				payload, strlen(payload),
				KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] < 0) {
				if ((errno != ENOMEM) && (errno != EDQUOT))
					pr_fail_err(name, "add_key");
				break;
			}
#if defined(KEYCTL_SET_TIMEOUT)
			if (sys_keyctl(KEYCTL_SET_TIMEOUT, keys[n], 1) < 0)
				pr_fail_err(name, "keyctl KEYCTL_SET_TIMEOUT");
#endif
		}

		/* And manipulate the keys */
		for (i = 0; i < n; i++) {
			snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, instance, i);
#if defined(KEYCTL_DESCRIBE)
			if (sys_keyctl(KEYCTL_DESCRIBE, keys[i], description) < 0)
				pr_fail_err(name, "keyctl KEYCTL_DESCRIBE");
			if (!opt_do_run)
				break;
#endif

			snprintf(payload, sizeof(payload),
				"somedata-%zu", n);
#if defined(KEYCTL_UPDATE)
			if (sys_keyctl(KEYCTL_UPDATE, keys[i],
			    payload, strlen(payload)) < 0) {
				if ((errno != ENOMEM) && (errno != EDQUOT))
					pr_fail_err(name, "keyctl KEYCTL_UPDATE");
			}
			if (!opt_do_run)
				break;
#endif

#if defined(KEYCTL_READ)
			memset(payload, 0, sizeof(payload));
			if (sys_keyctl(KEYCTL_READ, keys[i],
			    payload, sizeof(payload)) < 0)
				pr_fail_err(name, "keyctl KEYCTL_READ");
			if (!opt_do_run)
				break;
#endif

#if defined(__NR_request_key)
			snprintf(description, sizeof(description),
				"stress-ng-key-%u-%" PRIu32
				"-%zu", ppid, instance, i);
			if (sys_request_key("user", description, NULL,
				KEY_SPEC_PROCESS_KEYRING) < 0) {
				pr_fail_err(name, "request_key");
			}
#endif


#if defined(KEYCTL_CHOWN)
			(void)sys_keyctl(KEYCTL_CHOWN, keys[i], getuid(), -1);
			(void)sys_keyctl(KEYCTL_CHOWN, keys[i], -1, getgid());
#endif

#if defined(KEYCTL_SETPERM)
			(void)sys_keyctl(KEYCTL_SETPERM, keys[i], KEY_USR_ALL);
#endif

#if defined(KEYCTL_CLEAR)
			(void)sys_keyctl(KEYCTL_CLEAR, keys[i]);
#endif
#if defined(KEYCTL_REVOKE)
			if (mwc32() & 1)
				(void)sys_keyctl(KEYCTL_REVOKE, keys[i]);
#endif
#if defined(KEYCTL_INVALIDATE)
			(void)sys_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
			(*counter)++;
		}
		/* If we hit too many errors and bailed out early, clean up */
		while (i < n) {
#if defined(KEYCTL_CLEAR)
			(void)sys_keyctl(KEYCTL_CLEAR, keys[i]);
#endif
#if defined(KEYCTL_INVALIDATE)
			(void)sys_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
			i++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
