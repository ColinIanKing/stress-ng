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

#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE	600

static const help_t help[] = {
	{ NULL,	"crypt N",	"start N workers performing password encryption" },
	{ NULL,	"crypt-ops N",	"stop after N bogo crypt operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_CRYPT) &&	\
    defined(HAVE_CRYPT_H)

/*
 *  stress_crypt_id()
 *	crypt a password with given seed and id
 */
static int stress_crypt_id(
	const args_t *args,
	const char id,
	const char *method,
	const char *passwd,
	char *salt)
{
	salt[1] = id;
	char *crypted;
#if defined (HAVE_CRYPT_R)
	static struct crypt_data data;

	(void)memset(&data, 0, sizeof(data));
	crypted = crypt_r(passwd, salt, &data);
#else
	crypted = crypt(passwd, salt);
#endif
	if (!crypted) {
		pr_fail("%s: cannot encrypt with %s", args->name, method);
		return -1;
	}
	return 0;
}

/*
 *  stress_crypt()
 *	stress libc crypt
 */
static int stress_crypt(const args_t *args)
{
	do {
		static const char seedchars[] =
			"./0123456789ABCDEFGHIJKLMNOPQRST"
			"UVWXYZabcdefghijklmnopqrstuvwxyz";
		char passwd[16];
		char salt[] = "$x$........";
		uint64_t seed[2];
		size_t i;

		seed[0] = mwc64();
		seed[1] = mwc64();

		for (i = 0; i < 8; i++)
			salt[i + 3] = seedchars[(seed[i / 5] >> (i % 5) * 6) & 0x3f];
		for (i = 0; i < sizeof(passwd) - 1; i++)
			passwd[i] = seedchars[mwc32() % sizeof(seedchars)];
		passwd[i] = '\0';

		if (stress_crypt_id(args, '1', "MD5", passwd, salt) < 0)
			break;
#if NEED_GLIBC(2,7,0)
		if (stress_crypt_id(args, '5', "SHA-256", passwd, salt) < 0)
			break;
		if (stress_crypt_id(args, '6', "SHA-512", passwd, salt) < 0)
			break;
#endif
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_crypt_info = {
	.stressor = stress_crypt,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_crypt_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
