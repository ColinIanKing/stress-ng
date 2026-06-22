/*
 * Copyright (C) 2025 Gianmarco Lusvardi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#if defined(__linux__)
#include <stdio.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>

/* The following functions from libnl-genl-3 are used by stress-ng */

static void *lnl_genl_funcs[] = {
	(void *)genl_connect,
	(void *)genl_ctrl_resolve,
};

int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(lnl_genl_funcs) / sizeof(lnl_genl_funcs[0]); i++)
		printf("%p\n", lnl_genl_funcs[i]);
	return 0;
}
#endif /* __linux__ */
