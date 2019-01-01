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
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/ipc.h>
#include <sys/msg.h>

#if defined(__gnu_hurd__)
#error msgsnd, msgrcv, msgget, msgctl are not implemented
#endif

#define MAX_SIZE        (8)

typedef struct {
	long mtype;
	char msg[MAX_SIZE];
} msg_t;

int main(int argc, char **argv)
{
	int msgq_id;
	msg_t msg;
	int ret;
	struct msqid_ds buf;
#if defined(__linux__)
	struct msginfo info;
#endif

	/*
	 * This is not meant to be functionally
	 * correct, it is just used to check we
	 * can build minimal POSIX message queue
	 * based code
	 */
	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0)
		return -1;

	(void)strcpy(msg.msg, "TESTMSG");
	msg.mtype = 1;
	ret = msgsnd(msgq_id, &msg, sizeof(msg.msg), 0);
	(void)ret;
	ret = msgrcv(msgq_id, &msg, sizeof(msg.msg), 0, 0);
	(void)ret;
	ret = msgctl(msgq_id, IPC_STAT, &buf);
	(void)ret;
	ret = msgctl(msgq_id, IPC_RMID, NULL);
	(void)ret;
#if defined(__linux__)
	ret = msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info);
	(void)ret;
	ret = msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info);
	(void)ret;
#endif

	return 0;
}
