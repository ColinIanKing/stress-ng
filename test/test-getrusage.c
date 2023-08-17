// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/time.h>
#include <sys/resource.h>

int main(void)
{
	struct rusage usage;

	return getrusage(RUSAGE_SELF, &usage);
}
