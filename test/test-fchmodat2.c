// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */

#include <sys/stat.h>

int main(void)
{
	return fchmodat2(0, "", 0, 0);
}
