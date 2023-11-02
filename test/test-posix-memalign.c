// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdlib.h>

int main(void)
{
	void *alloc_buf;
	int ret;

	ret = posix_memalign((void **)&alloc_buf, 64, (size_t)1024);
	if (!ret)
		free(alloc_buf);

	return 0;
}
