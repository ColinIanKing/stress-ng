/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
	struct mmsghdr msg_hdr[2];
	struct iovec msg_iov1[2], msg_iov2[3];

	(void)memset(&addr, 0, sizeof(addr));
	(void)memset(&msg_iov1, 0, sizeof(msg_iov1));
	(void)memset(&msg_iov2, 0, sizeof(msg_iov2));
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

	msg_iov1[0].iov_base = "1";
	msg_iov1[0].iov_len = 1;
	msg_iov1[1].iov_base = "2";
	msg_iov1[1].iov_len = 1;

	msg_iov2[0].iov_base = "3";
	msg_iov2[0].iov_len = 1;
	msg_iov2[1].iov_base = "4";
	msg_iov2[1].iov_len = 1;
	msg_iov2[2].iov_base = "5";
	msg_iov2[2].iov_len = 1;

	msg_hdr[0].msg_hdr.msg_iov = msg_iov1;
	msg_hdr[0].msg_hdr.msg_iovlen = 2;
	msg_hdr[1].msg_hdr.msg_iov = msg_iov2;
	msg_hdr[1].msg_hdr.msg_iovlen = 3;

	ret = sendmmsg(sockfd, msg_hdr, 2, 0);
	(void)close(sockfd);

	return ret;
}
