// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include <linux/if_packet.h>

int main(void)
{
	struct tpacket_req3 tp;

	(void)tp;

	return sizeof(tp);
}
