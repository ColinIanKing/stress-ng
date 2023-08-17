// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define  _GNU_SOURCE

#include <sys/io.h>

int main(void)
{
	return iopl(0);
}
