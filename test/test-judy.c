// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <Judy.h>


int main(void)
{
	Pvoid_t PJLArray = (Pvoid_t)NULL;
	Word_t *pvalue;
	Word_t idx = 0;
	int rc;

	JLI(pvalue, PJLArray, idx);
	if (pvalue == PJERR) {
		JLD(rc, PJLArray, idx);
		return -1;
	}
	JLG(pvalue, PJLArray, idx);
	JLD(rc, PJLArray, idx);

	return 0;
}
