// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _XOPEN_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <crypt.h>

int main(void)
{
	char *ptr;
	static const char key[] = "keystring";
	static const char salt[] = "saltstring";
	static struct crypt_data data;

	ptr = crypt_r(key, salt, &data);

	return ptr != (void *)0;
}
