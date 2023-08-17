// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#include <sys/ioctl.h>
#include <net/if.h>

int main(void)
{
	struct ifreq ifr = { };

	(void)ifr;

	return sizeof(ifr);
}
