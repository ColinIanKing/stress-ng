// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
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

	(void)memset(&msg, 0, sizeof(msg));
	(void)strcpy(msg.msg, "TESTMSG");
	msg.mtype = 1;
	ret = msgsnd(msgq_id, &msg, sizeof(msg.msg), 0);
	(void)ret;
	ret = msgrcv(msgq_id, &msg, sizeof(msg.msg), 0, 0);
	(void)ret;
	ret = msgctl(msgq_id, IPC_STAT, &buf);
	(void)ret;
	/* pass &buf to stop coverity complaining */
	ret = msgctl(msgq_id, IPC_RMID, &buf);
	(void)ret;
#if defined(__linux__)
	ret = msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info);
	(void)ret;
	ret = msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info);
	(void)ret;
#endif

	return 0;
}
