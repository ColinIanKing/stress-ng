/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include "stress-ng.h"

#if !defined(__gnu_hurd__)

#define MAX_SIZE	(8)
#define MSG_STOP	"STOPMSG"

typedef struct {
	long mtype;
	char msg[MAX_SIZE];
} msg_t;

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
		pr_failed_dbg(name, "msgget");
		return EXIT_FAILURE;
	}
	pr_dbg(stderr, "System V message queue created, id: %d\n", msgq_id);

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		for (;;) {
			msg_t msg;
			uint64_t i;

			for (i = 0; ; i++) {
				uint64_t v;
				if (msgrcv(msgq_id, &msg, sizeof(msg.msg), 0, 0) < 0) {
					pr_failed_dbg(name, "msgrcv");
					break;
				}
				if (!strcmp(msg.msg, MSG_STOP))
					break;
				if (opt_flags & OPT_FLAGS_VERIFY) {
					memcpy(&v, msg.msg, sizeof(v));
					if (v != i)
						pr_fail(stderr, "msgrcv: expected msg containing 0x%" PRIx64
							" but received 0x%" PRIx64 " instead\n", i, v);
				}
			}
			exit(EXIT_SUCCESS);
		}
	} else {
		msg_t msg;
		uint64_t i = 0;
		int status;

		/* Parent */
		do {
			memcpy(msg.msg, &i, sizeof(i));
			msg.mtype = 1;
			if (msgsnd(msgq_id, &msg, sizeof(i), 0) < 0) {
				if (errno != EINTR)
					pr_failed_dbg(name, "msgsnd");
				break;
			}
			i++;
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		strncpy(msg.msg, MSG_STOP, sizeof(msg.msg));
		if (msgsnd(msgq_id, &msg, sizeof(msg.msg), 0) < 0)
			pr_failed_dbg(name, "termination msgsnd");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);

		if (msgctl(msgq_id, IPC_RMID, NULL) < 0)
			pr_failed_dbg(name, "msgctl");
		else
			pr_dbg(stderr, "System V message queue deleted, id: %d\n", msgq_id);
	}
	return EXIT_SUCCESS;
}

#endif
