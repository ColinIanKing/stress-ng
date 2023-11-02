// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <sys/socket.h>
#include <sys/un.h>

int main(void)
{
	struct sockaddr_un addr;

	(void)addr;

	return sizeof(addr);
}
