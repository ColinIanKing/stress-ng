// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#include <linux/fd.h>

int main(void)
{
	struct floppy_write_errors errors;

	return sizeof(errors);
}
