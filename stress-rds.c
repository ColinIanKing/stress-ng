/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

 #include <netdb.h>


#define RDS_BUF		(32)


static const help_t help[] = {
	{ NULL,	"rds N",	"start N workers performing RDP send/receives " },
	{ NULL,	"rds-ops N",	"stop after N rdp bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_rds
 *	stress by heavy rds ops
 */
static int stress_rds(const args_t *args)
{
	const int rds_port = 5000 + args->instance;
	pid_t pid;
	int rc = EXIT_SUCCESS;
	int ret;
	char hostname[HOST_NAME_MAX];
	struct sockaddr_in addr;
	socklen_t addrlen;

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		pr_inf("%s: cannot get host name, skipping\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(rds_port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addrlen = sizeof(addr);
	
	pr_dbg("%s: process [%d] using rds port %d\n",
		args->name, (int)args->pid, rds_port);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct hostent *entry;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		entry = gethostbyname("localhost");
		memcpy(&addr.sin_addr, entry->h_addr_list[0], entry->h_length);
		

		do {
			char buffer[RDS_BUF];
			int fd;
			int j = 0;

			if ((fd = socket(PF_RDS, SOCK_SEQPACKET, 0)) < 0) {
				pr_fail_dbg("socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			if (bind(fd, &addr, sizeof(struct sockaddr)) < 0) {
				pr_fail_dbg("bind");
				rc = EXIT_FAILURE;
				goto die_close;
			}

			do {
				ssize_t ret;
				struct msghdr msg;
				struct iovec iov;

				(void)memset(buffer, 'A' + (j++ % 26), sizeof(buffer));

				(void)memset(&msg, 0, sizeof(msg));
				msg.msg_name  = (struct sockaddr *)&addr;
				msg.msg_namelen = addrlen;
				msg.msg_iovlen = 1;
				msg.msg_iov = &iov;
				iov.iov_base = buffer;
				iov.iov_len = sizeof(buffer);
				
				ret = sendmsg(fd, &msg, 0);
				if (ret < 0) {	
					if (errno == EAGAIN)
						continue;
					if ((errno == EINTR) || (errno == ENETUNREACH))
						break;
					pr_fail_dbg("sendmsg");
					break;
				}
				printf("GOT DATA!\n");
			} while (keep_stressing());
			(void)close(fd);
		} while (keep_stressing());

		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);
		_exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */

		char buffer[RDS_BUF];
		int fd, status;
		int sockopt = 1;

		(void)setpgid(pid, g_pgrp);

		if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(PF_RDS, SOCK_SEQPACKET, 0)) < 0) {
			pr_fail_dbg("socket");
			rc = EXIT_FAILURE;
			goto die;
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) < 0) {
			pr_fail_dbg("setsockopt SO_REUSEADDR");
			rc = EXIT_FAILURE;
			goto die;
		}


		if (bind(fd, &addr, sizeof(struct sockaddr)) < 0) {
			pr_fail_dbg("bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		do {
			ssize_t n;
			struct msghdr msg;
			struct iovec iov;

			(void)memset(&msg, 0, sizeof(msg));
			msg.msg_name  = (struct sockaddr *)&addr;
			msg.msg_namelen = addrlen;
			msg.msg_iovlen = 1;
			msg.msg_iov = &iov;
			iov.iov_base = buffer;
			iov.iov_len = sizeof(buffer);
				
 			n = recvmsg(fd, &msg, 0);
			printf("n = %zd %d %s\n", n, errno, strerror(errno));
			if (n == 0)
				break;
			if (n < 0) {
				if (errno == EAGAIN)
					continue;
				if (errno != EINTR)
					pr_fail_dbg("recvfrom");
				break;
			}
			inc_counter(args);
		} while (keep_stressing());

die_close:
		(void)close(fd);
die:
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		}
	}
	return rc;
}

stressor_info_t stress_rds_info = {
	.stressor = stress_rds,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
