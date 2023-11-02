// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdlib.h>

#define NORETURN	__attribute__ ((noreturnx))

void NORETURN do_exit(void)
{
	exit(0);
}

int main(int argc, char **argv)
{
	do_exit();

	return 0;
}
