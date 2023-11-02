// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <netinet/ip.h>

int main(void)
{
	struct iphdr my_iphdr = { };

	(void)my_iphdr;

	return sizeof(struct iphdr);
}
