/*
 * Copyright (C) 2025      Colin Ian King
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

#define _GNU_SOURCE

#include <unistd.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

int main(void)
{
	int sockfd, ret;
	struct sockaddr_in addr;
	struct msghdr msg_hdr;
	struct iovec vec[1];

	(void)memset(&addr, 0, sizeof(addr));
	(void)memset(&msg_hdr, 0, sizeof(msg_hdr));

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return 1;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(9999);
	if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		(void)close(sockfd);
		return 1;
	}

	vec[0].iov_base = "test";
	vec[0].iov_len = 4;

	msg_hdr.msg_iov = vec;
	msg_hdr.msg_iovlen = 1;

	ret = sendmsg(sockfd, &msg_hdr, 0);
	(void)close(sockfd);

	return ret;
}
