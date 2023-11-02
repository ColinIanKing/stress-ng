// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <asm/prctl.h>
#include <sys/prctl.h>

extern int arch_prctl();

int main(void)
{
	unsigned long setting;

	return arch_prctl(ARCH_GET_CPUID, &setting);
}
