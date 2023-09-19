// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#if defined(__linux__)
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/unix_diag.h>
#endif

#if defined(AF_NETLINK) &&		\
    defined(NETLINK_SOCK_DIAG) &&	\
    defined(SOCK_DIAG_BY_FAMILY) &&	\
    defined(NLM_F_REQUEST) &&		\
    defined(NLM_F_DUMP) &&		\
    defined(UDIAG_SHOW_NAME) &&		\
    defined(UDIAG_SHOW_VFS) &&		\
    defined(UDIAG_SHOW_PEER) &&		\
    defined(UDIAG_SHOW_ICONS) &&	\
    defined(UDIAG_SHOW_RQLEN) &&	\
    defined(UDIAG_SHOW_MEMINFO)

int main(void)
{
	int unix_diag[] = {
		UNIX_DIAG_NAME,
		UNIX_DIAG_PEER
	};

	(void)unix_diag;

	return 0;
}

#else

#error sock_diag not supported

#endif
