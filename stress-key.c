/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

#if defined(HAVE_KEYUTILS_H)
#include <keyutils.h>
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"key N",	"start N workers exercising key operations" },
	{ NULL,	"key-ops N",	"stop after N key bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__NR_add_key)
#define HAVE_ADD_KEY
#endif

#if defined(__NR_keyctl)
#define HAVE_KEYCTL
#endif

#if defined(__NR_request_key)
#define HAVE_REQUEST_KEY
#endif


/*
 *  Note we don't need HAVE_REQUEST_KEY to be defined
 *  as this is an optional extra system call that can
 *  be overlooked in this stressor if it is not available
 */
#if defined(HAVE_KEYUTILS_H) && \
    defined(HAVE_ADD_KEY) &&	\
    defined(HAVE_KEYCTL) &&	\
    defined(HAVE_SYSCALL)

#define MAX_KEYS 		(256)
#define KEYCTL_TIMEOUT		(2)
#define KEY_HUGE_DESC_SIZE	(65536)

/*
 *  shim_keyctl()
 *	wrapper for the keyctl system call
 */
static long shim_keyctl(int cmd, ...)
{
	va_list args;
	long int arg0, arg1, arg2, arg3, ret;

	va_start(args, cmd);
	arg0 = va_arg(args, long int);
	arg1 = va_arg(args, long int);
	arg2 = va_arg(args, long int);
	arg3 = va_arg(args, long int);
	ret = syscall(__NR_keyctl, cmd, arg0, arg1, arg2, arg3);
	va_end(args);

	return ret;
}

/*
 *  shim_add_key()
 *	wrapper for the add_key system call
 */
static key_serial_t shim_add_key(
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
/*
 *  shim_request_key()
 *	wrapper for the request_key system call
 */
static key_serial_t shim_request_key(
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
static int stress_key(const stress_args_t *args)
{
	key_serial_t ALIGN64 keys[MAX_KEYS];
	pid_t ppid = getppid();
	int rc = EXIT_SUCCESS;
	bool timeout_supported = true;
	bool no_error = true;
	char *huge_description;
	const size_t key_huge_desc_size = STRESS_MAXIMUM(args->page_size, KEY_HUGE_DESC_SIZE) + 1024;

	huge_description = malloc(key_huge_desc_size);
	if (!huge_description) {
		pr_inf_skip("%s: cannot allocate %zd byte description string, skipping stressor\n",
			args->name, key_huge_desc_size);
		return EXIT_NO_RESOURCE;
	}
	stress_rndstr(huge_description, key_huge_desc_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i = 0, n = 0;
		char ALIGN64 description[64];
		char ALIGN64 payload[64];

		/* Add as many keys as we are allowed */
		for (n = 0; n < MAX_KEYS; n++) {
			size_t payload_len;

			(void)snprintf(payload, sizeof(payload),
				"somedata-%zu", n);
			payload_len = strlen(payload);
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%" PRIdMAX "-%" PRIu32
				"-%zu", (intmax_t)ppid, args->instance, n);


#if defined(KEYCTL_INVALIDATE)
			/* Exericse add_key with invalid long description */
			keys[n] = shim_add_key("user", huge_description, payload,
					payload_len, KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] >= 0)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[n]);
#endif

#if defined(KEYCTL_INVALIDATE)
			/* Exercise add_key with invalid empty description */
			keys[n] = shim_add_key("user", "", payload,
					payload_len, KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] >= 0)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[n]);
#endif

#if defined(KEYCTL_INVALIDATE)
			/* Exercise add_key with invalid description for keyring */
			keys[n] = shim_add_key("keyring", ".bad", payload,
					payload_len, KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] >= 0)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[n]);
#endif

#if defined(KEYCTL_INVALIDATE)
			/* Exercise add_key with invalid payload */
			keys[n] = shim_add_key("user", description, "",
					0, KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] >= 0)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[n]);
#endif

#if defined(KEYCTL_INVALIDATE)
			/* Exercise add_key with invalid payload length */
			keys[n] = shim_add_key("user", description, payload,
					SIZE_MAX, KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] >= 0)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[n]);
#endif

			keys[n] = shim_add_key("user", description,
				payload, payload_len,
				KEY_SPEC_PROCESS_KEYRING);
			if (keys[n] < 0) {
				if (errno == ENOSYS) {
					if (args->instance == 0)
						pr_inf_skip("%s: skipping stressor, add_key not implemented\n",
							args->name);
					no_error = false;
					rc = EXIT_NOT_IMPLEMENTED;
					goto tidy;
				}
				if ((errno == ENOMEM) || (errno == EDQUOT))
					break;
				pr_fail("%s: add_key failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto tidy;
			}
#if defined(KEYCTL_SET_TIMEOUT)
			if (timeout_supported) {
				if (shim_keyctl(KEYCTL_SET_TIMEOUT, keys[n], KEYCTL_TIMEOUT) < 0) {
					/* Some platforms don't support this */
					if (errno == ENOSYS) {
						timeout_supported = false;
					} else {
						pr_fail("%s: keyctl KEYCTL_SET_TIMEOUT failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
				}
			}
#endif
#if defined(KEYCTL_SEARCH)
			(void)shim_keyctl(KEYCTL_SEARCH, KEY_SPEC_PROCESS_KEYRING, "user", description, 0);
#endif
			if (!keep_stressing_flag())
				goto tidy;
		}

		/* And manipulate the keys */
		for (i = 0; i < n; i++) {
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%" PRIdMAX "-%" PRIu32
				"-%zu", (intmax_t)ppid, args->instance, i);
#if defined(KEYCTL_DESCRIBE)
			if (shim_keyctl(KEYCTL_DESCRIBE, keys[i], description) < 0)
				if ((errno != ENOMEM) &&
#if defined(EKEYEXPIRED)
				    (errno != EKEYEXPIRED) &&
#endif
#if defined(ENOKEY)
				    (errno != ENOKEY) &&
#endif
				    (errno != EDQUOT)) {
					pr_fail("%s: keyctl KEYCTL_DESCRIBE failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
			if (!keep_stressing_flag())
				goto tidy;
#endif

			(void)snprintf(payload, sizeof(payload),
				"somedata-%zu", n);
#if defined(KEYCTL_UPDATE)
			if (shim_keyctl(KEYCTL_UPDATE, keys[i],
			    payload, strlen(payload)) < 0) {
				if ((errno != ENOMEM) &&
#if defined(EKEYEXPIRED)
				    (errno != EKEYEXPIRED) &&
#endif
#if defined(ENOKEY)
				    (errno != ENOKEY) &&
#endif
				    (errno != EDQUOT)) {
					pr_fail("%s: keyctl KEYCTL_UPDATE failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
			if (!keep_stressing_flag())
				goto tidy;
#endif

#if defined(KEYCTL_READ)
			(void)memset(payload, 0, sizeof(payload));
			if (shim_keyctl(KEYCTL_READ, keys[i],
			    payload, sizeof(payload)) < 0) {
				if ((errno != ENOMEM) &&
#if defined(EKEYEXPIRED)
				    (errno != EKEYEXPIRED) &&
#endif
#if defined(ENOKEY)
				    (errno != ENOKEY) &&
#endif
				    (errno != EDQUOT)) {
					pr_fail("%s: keyctl KEYCTL_READ failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
			if (!keep_stressing_flag())
				goto tidy;
#endif

#if defined(HAVE_REQUEST_KEY)
			(void)snprintf(description, sizeof(description),
				"stress-ng-key-%" PRIdMAX "-%" PRIu32
				"-%zu", (intmax_t)ppid, args->instance, i);
			if (shim_request_key("user", description, NULL,
				KEY_SPEC_PROCESS_KEYRING) < 0) {
				if ((errno != ENOMEM) &&
#if defined(EKEYEXPIRED)
				    (errno != EKEYEXPIRED) &&
#endif
#if defined(ENOKEY)
				    (errno != ENOKEY) &&
#endif
				    (errno != EDQUOT)) {
					pr_fail("%s: request_key failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}

			/* exercise invalid type */
			(void)shim_request_key("_INVALID_TYPE_", description, NULL,
				KEY_SPEC_PROCESS_KEYRING);

			/* exercise invalid description */
			(void)shim_request_key("user", huge_description, NULL,
				KEY_SPEC_PROCESS_KEYRING);

			/* exercise invalid callout info */
			(void)shim_request_key("user", description, huge_description,
				KEY_SPEC_PROCESS_KEYRING);

			/* exercise invalid dest keyring id */
			(void)shim_request_key("user", description, NULL, INT_MIN);

			if (!keep_stressing_flag())
				goto tidy;
#endif

#if defined(KEYCTL_GET_SECURITY)
			{
				char buf[128];

				(void)shim_keyctl(KEYCTL_GET_SECURITY, keys[i], buf, sizeof(buf) - 1);
			}
#endif


#if defined(KEYCTL_CHOWN)
			(void)shim_keyctl(KEYCTL_CHOWN, keys[i], getuid(), -1);
			(void)shim_keyctl(KEYCTL_CHOWN, keys[i], -1, getgid());
#endif
#if defined(KEYCTL_CAPABILITIES)
			{
				char buf[1024];

				(void)shim_keyctl(KEYCTL_CAPABILITIES, buf, sizeof(buf));
			}
#endif
#if defined(KEYCTL_SETPERM)
			(void)shim_keyctl(KEYCTL_SETPERM, keys[i], KEY_USR_ALL);
#endif
#if defined(KEYCTL_LINK)
			(void)shim_keyctl(KEYCTL_LINK, keys[i], KEY_SPEC_PROCESS_KEYRING);
#endif
#if defined(KEYCTL_UNLINK)
			(void)shim_keyctl(KEYCTL_UNLINK, keys[i], KEY_SPEC_PROCESS_KEYRING);
#endif
#if defined(KEYCTL_REVOKE)
			if (stress_mwc1())
				(void)shim_keyctl(KEYCTL_REVOKE, keys[i]);
#endif
#if defined(KEYCTL_INVALIDATE)
			(void)shim_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
		}

		{
			char buf[4096];

			VOID_RET(ssize_t, system_read("/proc/keys", buf, sizeof(buf)));
			VOID_RET(ssize_t, system_read("/proc/key-users", buf, sizeof(buf)));
		}

		/*
		 *  Perform invalid keyctl command
		 */
		(void)shim_keyctl(~0, 0, 0, 0, 0);

tidy:
		inc_counter(args);
		/* If we hit too many errors and bailed out early, clean up */
		for (i = 0; i < n; i++) {
			if (keys[i] >= 0) {
#if defined(KEYCTL_INVALIDATE)
				(void)shim_keyctl(KEYCTL_INVALIDATE, keys[i]);
#endif
			}
		}
#if defined(KEYCTL_CLEAR)
		(void)shim_keyctl(KEYCTL_CLEAR, KEY_SPEC_PROCESS_KEYRING);
#endif
	} while (no_error && keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(huge_description);

	return rc;
}

stressor_info_t stress_key_info = {
	.stressor = stress_key,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_key_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without keyutils.h, add_key(), keyctl() or syscall() support"
};
#endif
