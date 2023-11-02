// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE
#define _XOPEN_SOURCE	600

#include <string.h>
#include <crypt.h>

int main(void)
{
	static const char passwd[] = "somerandomtext";
	static const char salt[] = "examplesalt";
	char *encrypted;
#if defined (__linux__)
	static struct crypt_data data;

	(void)memset(&data, 0, sizeof(data));
	encrypted = crypt_r(passwd, salt, &data);
#else
	encrypted = crypt(passwd, salt);
#endif
	(void)encrypted;

	return 0;
}
