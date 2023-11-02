// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include "zlib.h"

int main(void)
{
	z_stream strm;

	(void)deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	(void)deflateEnd(&strm);

	return 0;
}
