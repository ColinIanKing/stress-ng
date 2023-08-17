// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/sctp.h>

/* The following functions from libsctp are used by stress-ng */

static void *sctp_funcs[] = {
	(void *)sctp_sendmsg,
	(void *)sctp_recvmsg,
};

#if !defined(SOL_SCTP)
#error no SOL_SCTP
#endif

#if !defined(IPPROTO_SCTP)
#error no IPPROTO_SCTP
#endif

int main(void)
{
	return 0;
}
