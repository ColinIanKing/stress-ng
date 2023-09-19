// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>

int main(void)
{
	struct rusage usage;

	(void)memset(&usage, 0, sizeof(usage));

	return usage.ru_minflt;
}
