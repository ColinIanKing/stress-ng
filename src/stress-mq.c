/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"mq N",		"start N workers passing messages using POSIX messages" },
	{ NULL,	"mq-ops N",	"stop mq workers after N bogo messages" },
	{ NULL,	"mq-size N",	"specify the size of the POSIX message queue" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_mq_size(const char *opt)
{
	uint64_t sz;
	int mq_size;

	sz = stress_get_uint64(opt);
	stress_check_range("mq-size", sz, MIN_MQ_SIZE, MAX_MQ_SIZE);
	mq_size = (int)sz;
	return stress_set_setting("mq-size", TYPE_ID_INT, &mq_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mq_size,	stress_set_mq_size },
	{ 0,		NULL }
};

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) &&	\
    defined(HAVE_MQ_POSIX)

#define PRIOS_MAX	(128)

typedef struct {
	uint64_t	value;
} stress_msg_t;

static void stress_mq_notify_func(union sigval s)
{
	(void)s;
}

#if defined(SIGUSR2)
static void MLOCKED_TEXT stress_sigusr2_handler(int signum)
{
	(void)signum;
}
#endif

/*
 *  stress_mq
 *	stress POSIX message queues
 */
static int stress_mq(const stress_args_t *args)
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

#if defined(SIGUSR2)
	if (stress_sighandler(args->name, SIGUSR2, stress_sigusr2_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif

	if (!stress_get_setting("mq-size", &mq_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mq_size = MAX_MQ_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mq_size = MIN_MQ_SIZE;
	}
	sz = mq_size;

	(void)snprintf(mq_name, sizeof(mq_name), "/%s-%" PRIdMAX "-%" PRIu32,
		args->name, (intmax_t)args->pid, args->instance);
	fp = fopen("/proc/sys/fs/mqueue/msg_default", "r");
	if (fp) {
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
		attr.mq_msgsize = sizeof(stress_msg_t);
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
		pr_fail("%s: mq_open failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
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
		pr_inf("%s: mq_timed send and receive skipped, can't get time, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else {
		do_timed = true;
		abs_timeout.tv_sec = time_start + g_opt_timeout + 1;
		abs_timeout.tv_nsec = 0;
	}


	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		struct sigevent sigev;
		uint64_t values[PRIOS_MAX];

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)memset(&values, 0, sizeof(values));

		while (keep_stressing_flag()) {
			uint64_t i;

			for (i = 0; ; i++) {
				stress_msg_t ALIGN64 msg;
				int ret;
				const uint64_t timed = (i & 1);
				unsigned int prio;

				if (!(i & 1023)) {
#if defined(__linux__)
					char buffer[1024];
					off_t off;
					struct stat statbuf;
					void *ptr;
#if defined(HAVE_POLL_H)
					struct pollfd fds[1];
#endif
					/* On Linux, one can seek on a mq descriptor */
					off = lseek(mq, 0, SEEK_SET);
					(void)off;

					/* Attempt a fstat too */
					ret = fstat(mq, &statbuf);
					(void)ret;

					/* illegal mmap, should be ENODEV */
					ptr = mmap(NULL, 16, PROT_READ, MAP_SHARED, mq, 0);
					if (ptr != MAP_FAILED)
						(void)munmap(ptr, 16);
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
					if (ret < 0) {
						if (errno == EINTR)
							break;
						pr_fail("%s: mq read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
#endif

					(void)memset(&sigev, 0, sizeof sigev);
					switch (stress_mwc8() % 5) {
					case 3:
						/* Illegal signal */
						sigev.sigev_notify = SIGEV_SIGNAL;
						sigev.sigev_signo = ~0;
						break;
					case 2:
						/* Illegal notify event */
						sigev.sigev_notify = ~0;
						break;
					case 1:
						sigev.sigev_notify = SIGEV_NONE;
						break;
					case 0:
#if defined(SIGUSR2)
						sigev.sigev_notify = SIGEV_SIGNAL;
						sigev.sigev_signo = SIGUSR2;
						break;
#else
						CASE_FALLTHROUGH;
#endif
					default:
						sigev.sigev_notify = SIGEV_THREAD;
						sigev.sigev_notify_function = stress_mq_notify_func;
						sigev.sigev_notify_attributes = NULL;
						break;
					}
					(void)mq_notify(mq, &sigev);
				}

				/*
				 * toggle between timedreceive and receive
				 */
				if (do_timed && (timed))
					ret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), &prio, &abs_timeout);
				else
					ret = mq_receive(mq, (char *)&msg, sizeof(msg), &prio);
				if (ret < 0) {
					if (errno != EINTR) {
						pr_fail("%s: %s failed, errno=%d (%s)\n",
							args->name,
							timed ? "mq_timedreceive" : "mq_receive",
							errno, strerror(errno));
					}
					break;
				}
				if (prio >= PRIOS_MAX) {
					pr_fail("%s: mq_receive: unexpected priority %u, expected 0..%d\n",
						args->name, prio, PRIOS_MAX - 1);
				} else {
					if (g_opt_flags & OPT_FLAGS_VERIFY) {
						if (msg.value != values[prio]) {
							pr_fail("%s: mq_receive: expected message "
								"containing 0x%" PRIx64
								" but received 0x%" PRIx64 " instead\n",
								args->name, values[prio], msg.value);
						}
						values[prio]++;
					}
				}
			}
			_exit(EXIT_SUCCESS);
		}
	} else {
		int status;
		int attr_count = 0;
		stress_msg_t ALIGN64 msg;
		uint64_t values[PRIOS_MAX];

		/* Parent */
		(void)setpgid(pid, g_pgrp);
		(void)memset(&msg, 0, sizeof(msg));

		(void)memset(&values, 0, sizeof(values));

		do {
			int ret;
			unsigned int prio = stress_mwc8() % PRIOS_MAX;
			const uint64_t timed = (msg.value & 1);

			if ((attr_count++ & 31) == 0) {
				if (mq_getattr(mq, &attr) < 0) {
					pr_fail("%s: mq_getattr failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				} else {
					struct mq_attr old_attr;

					(void)mq_setattr(mq, &attr, &old_attr);
				}

				(void)mq_getattr(~0, &attr);
			}

			msg.value = values[prio];
			values[prio]++;
			/*
			 * toggle between timedsend and send
			 */
			if (do_timed && (timed))
				ret = mq_timedsend(mq, (char *)&msg, sizeof(msg), prio, &abs_timeout);
			else
				ret = mq_send(mq, (char *)&msg, sizeof(msg), prio);
			if (ret < 0) {
				if ((errno != EINTR) && (errno != ETIMEDOUT))
					pr_fail("%s: %s failed, errno=%d (%s)\n",
						args->name,
						timed ? "mq_timedsend" : "mq_send",
						errno, strerror(errno));
				break;
			}
			inc_counter(args);
		} while (keep_stressing(args));

		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);

		if (mq_close(mq) < 0)
			pr_fail("%s: mq_close failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		if (mq_unlink(mq_name) < 0)
			pr_fail("%s: mq_unlink failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

		/* Exercise invalid mq_close, already closed mq */
		(void)mq_close(mq);
		/* Exercise invalid mq_unlink */
		(void)mq_unlink(mq_name);
		/* Exercise invalid mq_unlink */
		(void)mq_unlink("/");
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

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
