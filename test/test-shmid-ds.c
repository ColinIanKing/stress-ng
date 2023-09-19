// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#include <sys/ipc.h>
#include <sys/shm.h>

int main(void)
{
	struct shmid_ds ds = { };

	(void)ds;

	return 0;
}
