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
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <netlink/handlers.h>

/* The following functions from libnl-3 are used by stress-ng */

static void *lnl_funcs[] = {
	(void *)nl_cb_alloc,
	(void *)nl_cb_err,
	(void *)nl_cb_put,
	(void *)nl_cb_set,
	(void *)nl_geterror,
	(void *)nl_recvmsgs,
	(void *)nl_send_auto_complete,
	(void *)nl_socket_alloc,
	(void *)nl_socket_free,
	(void *)nl_socket_get_fd,
	(void *)nl_socket_set_buffer_size,
	(void *)nl_socket_set_cb,
};

int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(lnl_funcs) / sizeof(lnl_funcs[0]); i++)
		printf("%p\n", lnl_funcs[i]);
	return 0;
}
#endif /* __linux__ */
