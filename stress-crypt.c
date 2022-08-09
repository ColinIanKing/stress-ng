/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(HAVE_CRYPT_H)
#include <crypt.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"crypt N",	"start N workers performing password encryption" },
	{ NULL,	"crypt-ops N",	"stop after N bogo crypt operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_CRYPT) &&	\
    defined(HAVE_CRYPT_H)

typedef struct {
	const char id;
	const char *method;
} crypt_method_t;

static const crypt_method_t crypt_methods[] = {
	{ '1', "MD5" },
	{ '5', "SHA-256" },
	{ '6', "SHA-512" },
	{ '7', "scrypt" },
	{ '3', "NT" },
	{ 'y', "yescrypt" },
};

/*
 *  stress_crypt_id()
 *	crypt a password with given seed and id
 */
static int stress_crypt_id(
	const stress_args_t *args,
	const char id,
	const char *method,
	const char *passwd,
	char *salt)
{
	char *encrypted;
#if defined (HAVE_CRYPT_R)
	static struct crypt_data data;

	(void)memset(&data, 0, sizeof(data));
	errno = 0;
	salt[1] = id;
	encrypted = crypt_r(passwd, salt, &data);
#else
	salt[1] = id;
	encrypted = crypt(passwd, salt);
#endif
	if (!encrypted) {
		switch (errno) {
		case 0:
			break;
		case EINVAL:
			break;
#if defined(ENOSYS)
		case ENOSYS:
			break;
#endif
#if defined(EOPNOTSUPP)
		case EOPNOTSUPP:
#endif
			break;
		default:
			pr_fail("%s: cannot encrypt with %s, errno=%d (%s)\n",
				args->name, method, errno, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/*
 *  stress_crypt()
 *	stress libc crypt
 */
static int stress_crypt(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		static const char seedchars[] =
			"./0123456789ABCDEFGHIJKLMNOPQRST"
			"UVWXYZabcdefghijklmnopqrstuvwxyz";
		char passwd[16];
		char salt[] = "$x$........";
		uint64_t seed[2];
		size_t i, failed = 0;

		seed[0] = stress_mwc64();
		seed[1] = stress_mwc64();

		for (i = 0; i < 8; i++)
			salt[i + 3] = seedchars[(seed[i / 5] >> (i % 5) * 6) & 0x3f];
		for (i = 0; i < sizeof(passwd) - 1; i++)
			passwd[i] = seedchars[stress_mwc32() % sizeof(seedchars)];
		passwd[i] = '\0';

		for (i = 0; keep_stressing(args) && (i < SIZEOF_ARRAY(crypt_methods)); i++) {
			int ret;

			ret = stress_crypt_id(args,
					      crypt_methods[i].id,
					      crypt_methods[i].method,
					      passwd, salt);
			if (ret < 0)
				failed++;
			else
				inc_counter(args);
		}
		if (failed)
			break;
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_crypt_info = {
	.stressor = stress_crypt,
	.class = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_crypt_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
