// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stddef.h>
#include <stdio.h>

int main(void)
{
	FILE *fp;
	char *ptr = NULL;
	size_t size = 0;

	fp = open_memstream(&ptr, &size);
	if (!fp)
		return 0;
	fprintf(fp, "Test");
	(void)fclose(fp);

	return size;
}
