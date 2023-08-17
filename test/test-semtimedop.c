// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define  _GNU_SOURCE

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int main(void)
{
	int semid = 0;
	struct sembuf ops = { 0 };
	size_t nsops = 1;
	struct timespec timeout = { 0 };

	return semtimedop(semid, &ops, nsops, &timeout);
}
