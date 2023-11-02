// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <string.h>
#include <fcntl.h>
#include <aio.h>

int main(void)
{
	struct aiocb aio;

	(void)memset(&aio, 0, sizeof(aio));

	return aio_fsync(O_SYNC, &aio);
}
