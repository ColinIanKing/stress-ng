/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_MSG)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>

#define MAX_SIZE	(8)
#define MSG_STOP	"STOPMSG"

typedef struct {
	long mtype;
	char msg[MAX_SIZE];
} msg_t;

static int stress_msg_getstats(const char *name, const int msgq_id)
{
	struct msqid_ds buf;
#if defined(__linux__)
	struct msginfo info;
#endif

	if (msgctl(msgq_id, IPC_STAT, &buf) < 0) {
		pr_fail(stderr, "%s: msgctl: IPC_STAT failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}
#if defined(__linux__)
	if (msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info) < 0) {
		pr_fail(stderr, "%s: msgctl: IPC_STAT failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}
	if (msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info) < 0) {
		pr_fail(stderr, "%s: msgctl: IPC_STAT failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -errno;
	}
#endif

	return 0;
}

/*
 *  stress_msg
 *	stress by message queues
 */
int stress_msg(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int msgq_id;

	(void)instance;

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0) {
		pr_fail_dbg(name, "msgget");
		return exit_status(errno);
	}
	pr_dbg(stderr, "System V message queue created, id: %d\n", msgq_id);

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		while (opt_do_run) {
			msg_t msg;
			uint64_t i;

			for (i = 0; ; i++) {
				uint64_t v;
				if (msgrcv(msgq_id, &msg, sizeof(msg.msg), 0, 0) < 0) {
					pr_fail_dbg(name, "msgrcv");
					break;
				}
				if (!strcmp(msg.msg, MSG_STOP))
					break;
				if (opt_flags & OPT_FLAGS_VERIFY) {
					memcpy(&v, msg.msg, sizeof(v));
					if (v != i)
						pr_fail(stderr, "%s: msgrcv: expected msg containing 0x%" PRIx64
							" but received 0x%" PRIx64 " instead\n", name, i, v);
				}
			}
			exit(EXIT_SUCCESS);
		}
	} else {
		msg_t msg;
		uint64_t i = 0;
		int status;

		/* Parent */
		setpgid(pid, pgrp);

		do {
			memcpy(msg.msg, &i, sizeof(i));
			msg.mtype = 1;
			if (msgsnd(msgq_id, &msg, sizeof(i), 0) < 0) {
				if (errno != EINTR)
					pr_fail_dbg(name, "msgsnd");
				break;
			}
			if ((i & 0x1f) == 0)
				if (stress_msg_getstats(name, msgq_id) < 0)
					break;
			i++;
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		strncpy(msg.msg, MSG_STOP, sizeof(msg.msg));
		if (msgsnd(msgq_id, &msg, sizeof(msg.msg), 0) < 0)
			pr_fail_dbg(name, "termination msgsnd");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);

		if (msgctl(msgq_id, IPC_RMID, NULL) < 0)
			pr_fail_dbg(name, "msgctl");
		else
			pr_dbg(stderr, "System V message queue deleted, id: %d\n", msgq_id);
	}
	return EXIT_SUCCESS;
}

#endif
