// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <linux/fd.h>

int main(void)
{
	struct floppy_fdc_state state;

	return sizeof(state);
}
