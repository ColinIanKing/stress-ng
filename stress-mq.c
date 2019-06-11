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

static const help_t help[] = {
	{ NULL,	"mq N",		"start N workers passing messages using POSIX messages" },
	{ NULL,	"mq-ops N",	"stop mq workers after N bogo messages" },
	{ NULL,	"mq-size N",	"specify the size of the POSIX message queue" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_mq_size(const char *opt)
{
	uint64_t sz;
	int mq_size;

	sz = get_uint64(opt);
	check_range("mq-size", sz, MIN_MQ_SIZE, MAX_MQ_SIZE);
	mq_size = (int)sz;
	return set_setting("mq-size", TYPE_ID_INT, &mq_size);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_mq_size,	stress_set_mq_size },
	{ 0,		NULL }
};

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX)

typedef struct {
	uint64_t	value;
} msg_t;

static void stress_mq_notify_func(union sigval s)
{
	(void)s;
}

/*
 *  stress_mq
 *	stress POSIX message queues
 */
static int stress_mq(const args_t *args)
{
	pid_t pid;
	mqd_t mq = -1;
	int sz, max_sz, mq_size = DEFAULT_MQ_SIZE;
	FILE *fp;
	struct mq_attr attr;
	char mq_name[64];
	bool do_timed;
	time_t time_start;
	struct timespec abs_timeout;

	if (!get_setting("mq-size", &mq_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mq_size = MAX_MQ_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mq_size = MIN_MQ_SIZE;
	}
	sz = mq_size;

	(void)snprintf(mq_name, sizeof(mq_name), "/%s-%i-%" PRIu32,
		args->name, args->pid, args->instance);
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
		if (errno == ENOSYS) {
			pr_inf("%s: POSIX message queues not implemented, skipping stressor\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		sz--;
	}
	if (mq < 0) {
		pr_fail_dbg("mq_open");
		return EXIT_FAILURE;
	}
	if (sz < mq_size) {
		pr_inf("%s: POSIX message queue requested "
			"size %d messages, maximum of %d allowed\n",
			args->name, mq_size, sz);
	}
	pr_dbg("%s: POSIX message queue %s with %lu messages\n",
		args->name, mq_name, (unsigned long)attr.mq_maxmsg);

	if (time(&time_start) == ((time_t)-1)) {
		do_timed = false;
		pr_fail_dbg("mq_timed send and receive skipped, can't get time");
	} else {
		do_timed = true;
		abs_timeout.tv_sec = time_start + g_opt_timeout + 1;
		abs_timeout.tv_nsec = 0;
	}


again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		struct sigevent sigev;

		(void)memset(&sigev, 0, sizeof sigev);
		sigev.sigev_notify = SIGEV_THREAD;
		sigev.sigev_notify_function = stress_mq_notify_func;
		sigev.sigev_notify_attributes = NULL;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		while (g_keep_stressing_flag) {
			uint64_t i = 0;

			for (;;) {
				msg_t ALIGN64 msg;
				int ret;
				const uint64_t timed = (i & 1);

				if (!(i & 1023)) {
#if defined(__linux__)
					char buffer[1024];
					off_t off;
#if defined(HAVE_POLL_H)
					struct pollfd fds[1];
#endif
					/* On Linux, one can seek on a mq descriptor */
					off = lseek(mq, 0, SEEK_SET);
					(void)off;
#if defined(HAVE_POLL_H)
					/* ..and poll too */
					fds[0].fd = mq;
					fds[0].events = POLLIN;
					fds[0].revents = 0;
					ret = poll(fds, 1, 0);
					(void)ret;
#endif
					/* Read state of queue from mq fd */
					ret = read(mq, buffer, sizeof(buffer));
					if (ret < 0)
						pr_fail_dbg("mq read");
#endif

					(void)mq_notify(mq, &sigev);
				}

				/*
				 * toggle between timedreceive and receive
				 */
				if (do_timed && (timed))
					ret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL, &abs_timeout);
				else
					ret = mq_receive(mq, (char *)&msg, sizeof(msg), NULL);

				if (ret < 0) {
					pr_fail_dbg(timed ? "mq_timedreceive" : "mq_receive");
					break;
				}
				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					if (msg.value != i) {
						pr_fail("%s: mq_receive: expected message "
							"containing 0x%" PRIx64
							" but received 0x%" PRIx64 " instead\n",
							args->name, i, msg.value);
					}
				}
				i++;
			}
			_exit(EXIT_SUCCESS);
		}
	} else {
		int status;
		int attr_count = 0;
		msg_t ALIGN64 msg;

		/* Parent */
		(void)setpgid(pid, g_pgrp);
		(void)memset(&msg, 0, sizeof(msg));

		do {
			int ret;
			const uint64_t timed = (msg.value & 1);

			if ((attr_count++ & 31) == 0) {
				if (mq_getattr(mq, &attr) < 0)
					pr_fail_dbg("mq_getattr");
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
					pr_fail_dbg(timed ? "mq_timedsend" : "mq_send");
				break;
			}
			msg.value++;
			inc_counter(args);
		} while (keep_stressing());

		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);

		if (mq_close(mq) < 0)
			pr_fail_dbg("mq_close");
		if (mq_unlink(mq_name) < 0)
			pr_fail_dbg("mq_unlink");
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_mq_info = {
	.stressor = stress_mq,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_mq_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
