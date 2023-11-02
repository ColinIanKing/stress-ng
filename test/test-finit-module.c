// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Luis Chamberlain <mcgrof@kernel.org>
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include <linux/module.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern int finit_module(int fd, const char *param_values, int flags);

int main(void)
{
	const char *module = "hello";
	int fd;

	fd = open(module, O_RDONLY | O_CLOEXEC);
	if (fd > 0) {
		int ret;

		ret = finit_module(fd, "", 0);
		(void)ret;
	}

	return 0;
}
