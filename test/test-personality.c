// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/personality.h>

int main(void)
{
	return personality(0);
}

