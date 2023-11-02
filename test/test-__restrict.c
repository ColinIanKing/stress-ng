// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

void test(char * __restrict dst, const char * __restrict src, int n)
{
	int i;

	for (i = 0; *src && (i < n); i++)
		*dst++ = *src++;
}

int main(void)
{
	char buffer[1024];

	test(buffer, "hello world", sizeof(buffer));
}
