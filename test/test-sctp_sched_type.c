// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <netinet/sctp.h>

int main(void)
{
	/* Minimum expected scheduler types expected for sctp stressor */
	enum sctp_sched_type types[] = {
		SCTP_SS_FCFS,
		SCTP_SS_PRIO,
		SCTP_SS_RR,
	};
	return (int)sizeof(types) / sizeof(types[0]);
}
