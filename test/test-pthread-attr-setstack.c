// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <pthread.h>

int main(void)
{
	pthread_attr_t attr;
	int ret;
	static unsigned char stack[65536];

	ret = pthread_attr_init(&attr);
	if (ret)
		return -1;

	return pthread_attr_setstack(&attr, stack, sizeof(stack));
}
