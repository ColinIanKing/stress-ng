// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <mqueue.h>
#include <signal.h>

#if defined(__gnu_hurd__)
#error posix message queues not implemented on GNU/HURD
#endif
#if defined(__FreeBSD_kernel__)
#error posix message queues not implemented with FreeBSD kernel
#endif

typedef struct {
	unsigned int	value;
} msg_t;

static void notify_func(union sigval s)
{
	(void)s;
}

int main(int argc, char **argv)
{
	mqd_t mq;
	msg_t msg;
	struct mq_attr attr;
	int ret;
	struct timespec abs_timeout;
	struct sigevent sigev;
	char mq_name[64];

	attr.mq_flags = 0;
	attr.mq_maxmsg = 32;
	attr.mq_msgsize = sizeof(msg_t);
	attr.mq_curmsgs = 0;

	(void)snprintf(mq_name, sizeof(mq_name), "/%s-%i",
		argv[0], getpid());
	/*
	 * This is not meant to be functionally
	 * correct, it is just used to check we
	 * can build minimal POSIX message queue
	 * based code
	 */
	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0)
		return -1;

	(void)memset(&sigev, 0, sizeof sigev);
	sigev.sigev_notify = SIGEV_THREAD;
	sigev.sigev_notify_function = notify_func;
	sigev.sigev_notify_attributes = NULL;

	ret = mq_notify(mq, &sigev);
	(void)ret;
	(void)memset((void *)&abs_timeout, 0, sizeof(abs_timeout));
	ret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &abs_timeout);
	(void)ret;
	ret = mq_receive(mq, (char *)&msg, sizeof(msg), NULL);
	(void)ret;
	ret = mq_getattr(mq, &attr);
	(void)ret;
	(void)memset(&msg, 0, sizeof(msg));
	ret = mq_timedsend(mq, (char *)&msg, sizeof(msg), 1, &abs_timeout);
	(void)ret;
	ret = mq_send(mq, (char *)&msg, sizeof(msg), 1);
	(void)ret;
	ret = mq_close(mq);
	(void)ret;
	ret = mq_unlink(mq_name);
	(void)ret;

	return 0;
}
