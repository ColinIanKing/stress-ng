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

#if defined(STRESS_MQ)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <mqueue.h>
#include <time.h>

typedef struct {
	uint64_t	value;
	bool		stop;
} msg_t;

static int opt_mq_size = DEFAULT_MQ_SIZE;
static bool set_mq_size = false;

void stress_set_mq_size(const char *optarg)
{
	uint64_t sz;

	set_mq_size = true;
	sz = get_uint64_byte(optarg);
	opt_mq_size = (int)sz;
        check_range("mq-size", sz,
                MIN_MQ_SIZE, MAX_MQ_SIZE);
}

static void stress_mq_notify_func(union sigval s)
{
	(void)s;
}

/*
 *  stress_mq
 *	stress POSIX message queues
 */
int stress_mq(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid = getpid();
	mqd_t mq = -1;
	int sz, max_sz;
	FILE *fp;
	struct mq_attr attr;
	char mq_name[64];
	bool do_timed;
	time_t time_start;
	struct timespec abs_timeout;

	if (!set_mq_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_mq_size = MAX_MQ_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_mq_size = MIN_MQ_SIZE;
	}
	sz = opt_mq_size;

	snprintf(mq_name, sizeof(mq_name), "/%s-%i-%" PRIu32,
		name, pid, instance);
	if ((fp = fopen("/proc/sys/fs/mqueue/msg_default", "r")) != NULL) {
		if (fscanf(fp, "%20d", &max_sz) != 1)
			max_sz = MAX_MQ_SIZE;
		(void)fclose(fp);
		if (max_sz < MIN_MQ_SIZE)
			max_sz = MIN_MQ_SIZE;
		if (max_sz > MAX_MQ_SIZE)
			max_sz = MAX_MQ_SIZE;
	} else {
		max_sz = MAX_MQ_SIZE;
	}

	if (sz > max_sz)
		sz = max_sz;
	/*
	 *  Determine a workable MQ size if we can't determine it from /proc
	 */
	while (sz > 0) {
		attr.mq_flags = 0;
		attr.mq_maxmsg = sz;
		attr.mq_msgsize = sizeof(msg_t);
		attr.mq_curmsgs = 0;

		mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
		if (mq >= 0)
			break;
		sz--;
	}
	if (mq < 0) {
		pr_fail_dbg(name, "mq_open");
		return EXIT_FAILURE;
	}
	if (sz < opt_mq_size) {
		pr_inf(stdout, "%s: POSIX message queue requested "
			"size %d messages, maximum of %d allowed\n",
			name, opt_mq_size, sz);
	}
	pr_dbg(stderr, "POSIX message queue %s with %lu messages\n",
		mq_name, (unsigned long)attr.mq_maxmsg);

	if (time(&time_start) == ((time_t)-1)) {
		do_timed = false;
		pr_fail_dbg(name, "mq_timed send and receive skipped, can't get time");
	} else {
		do_timed = true;
		abs_timeout.tv_sec = time_start + opt_timeout + 1;
		abs_timeout.tv_nsec = 0;
	}


again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		struct sigevent sigev;

		memset(&sigev, 0, sizeof sigev);
		sigev.sigev_notify = SIGEV_THREAD;
		sigev.sigev_notify_function = stress_mq_notify_func;
		sigev.sigev_notify_attributes = NULL;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		while (opt_do_run) {
			uint64_t i = 0;

			for (;;) {
				msg_t msg;
				int ret;
				const uint64_t timed = (i & 1);

				if (!(i & 1023))
					mq_notify(mq, &sigev);

				/*
				 * toggle between timedreceive and receive
				 */
				if (do_timed && (timed))
					ret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &abs_timeout);
				else
					ret = mq_receive(mq, (char *)&msg, sizeof(msg), NULL);

				if (ret < 0) {
					pr_fail_dbg(name, timed ? "mq_timedreceive" : "mq_receive");
					break;
				}
				if (msg.stop)
					break;
				if (opt_flags & OPT_FLAGS_VERIFY) {
					if (msg.value != i) {
						pr_fail(stderr, "%s: mq_receive: expected message "
							"containing 0x%" PRIx64
							" but received 0x%" PRIx64 " instead\n",
							name, i, msg.value);
					}
				}
				i++;
			}
			exit(EXIT_SUCCESS);
		}
	} else {
		uint64_t i = 0;
		int status;
		int attr_count = 0;
		msg_t msg;

		/* Parent */
		setpgid(pid, pgrp);

		do {
			int ret;
			const uint64_t timed = (i & 1);

			memset(&msg, 0, sizeof(msg));
			msg.value = (*counter);
			msg.stop = false;
			if ((attr_count++ & 31) == 0) {
				struct mq_attr attr;

				if (mq_getattr(mq, &attr) < 0)
					pr_fail_dbg(name, "mq_getattr");
			}

			/*
			 * toggle between timedsend and send
			 */
			if (do_timed && (timed))
				ret = mq_timedsend(mq, (char *)&msg, sizeof(msg), 1, &abs_timeout);
			else
				ret = mq_send(mq, (char *)&msg, sizeof(msg), 1);

			if (ret < 0) {
				if (errno != EINTR)
					pr_fail_dbg(name, timed ? "mq_timedsend" : "mq_send");
				break;
			}
			i++;
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		msg.value = (*counter);
		msg.stop = true;

		if (mq_send(mq, (char *)&msg, sizeof(msg), 1) < 0) {
			pr_fail_dbg(name, "termination mq_send");
		}
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);

		if (mq_close(mq) < 0)
			pr_fail_dbg(name, "mq_close");
		if (mq_unlink(mq_name) < 0)
			pr_fail_dbg(name, "mq_unlink");
	}
	return EXIT_SUCCESS;
}

#endif
