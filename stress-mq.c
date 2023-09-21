// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#define MIN_MQ_SIZE		(1)
#define MAX_MQ_SIZE		(32)
#define DEFAULT_MQ_SIZE		(10)

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

/*
 *  stress_mq_invalid_open()
 *	perform invalid mq open and perform tidy up
 *	if it somehow succeeded
 */
static void stress_mq_invalid_open(
	const char *name,
	int oflag,
	mode_t mode,
	struct mq_attr *attr)
{
	mqd_t mq;

	mq = mq_open(name, oflag, mode, attr);
	if (mq >= 0) {
		(void)mq_close(mq);
		(void)mq_unlink(name);
	}
}

/*
 *  stress_mq
 *	stress POSIX message queues
 */
static int stress_mq(const stress_args_t *args)
{
	pid_t pid;
	mqd_t mq = -1;
	int sz, max_sz, mq_size = DEFAULT_MQ_SIZE, parent_cpu;
	FILE *fp;
	struct mq_attr attr;
	char mq_name[64], mq_tmp_name[64];
	bool do_timed;
	time_t time_start;
	struct timespec abs_timeout;
	unsigned int max_prio = UINT_MAX;

#if defined(SIGUSR2)
	if (stress_sighandler(args->name, SIGUSR2, stress_sighandler_nop, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif

#if defined(_SC_MQ_PRIO_MAX)
	{
		long sysconf_ret;

		sysconf_ret = sysconf(_SC_MQ_PRIO_MAX);
		if ((sysconf_ret > 0) && (sysconf_ret < (long)UINT_MAX))
			max_prio = (unsigned int)(sysconf_ret + 1);
	}
#endif

	if (!stress_get_setting("mq-size", &mq_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mq_size = MAX_MQ_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mq_size = MIN_MQ_SIZE;
	}
	sz = mq_size;

	(void)snprintf(mq_tmp_name, sizeof(mq_tmp_name), "/%s-%" PRIdMAX "-%" PRIu32,
		args->name, (intmax_t)args->pid, args->instance + 10000);

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
		if (errno == EMFILE) {
			pr_inf_skip("%s: system ran out of file descriptors, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
		if (errno == ENOSYS) {
			if (args->instance == 0)
				pr_inf_skip("%s: POSIX message queues not implemented, skipping stressor\n",
					args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		sz--;
	}
	if (mq < 0) {
		/*
		 *  With bulk testing we may run out of mq space, so skip rather
		 *  than hard fail
		 */
		if (errno == ENOSPC) {
			pr_inf_skip("%s: mq_open: no more free queue space, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
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
		abs_timeout.tv_sec = time_start + (time_t)g_opt_timeout + 1;
		abs_timeout.tv_nsec = 0;
	}


	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		(void)mq_close(mq);
		(void)mq_unlink(mq_name);

		if (!stress_continue(args))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		struct sigevent sigev;
		uint64_t values[PRIOS_MAX];

		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		(void)shim_memset(&values, 0, sizeof(values));

		while (stress_continue_flag()) {
			uint64_t i;

			for (i = 0; ; i++) {
				stress_msg_t ALIGN64 msg;
				ssize_t sret;
				const uint64_t timed = (i & 1);
				unsigned int prio;

				if (!(i & 1023)) {
#if defined(__linux__)
					char buffer[1024];
					struct stat statbuf;
					void *ptr;
#if defined(HAVE_POLL_H)
					struct pollfd fds[1];
#endif
					/* On Linux, one can seek on a mq descriptor */
					VOID_RET(off_t, lseek(mq, 0, SEEK_SET));

					/* Attempt a fstat too */
					VOID_RET(int, fstat(mq, &statbuf));

					/* illegal mmap, should be ENODEV */
					ptr = mmap(NULL, 16, PROT_READ, MAP_SHARED, mq, 0);
					if (ptr != MAP_FAILED)
						(void)munmap(ptr, 16);
#if defined(HAVE_POLL_H)
					/* ..and poll too */
					fds[0].fd = mq;
					fds[0].events = POLLIN;
					fds[0].revents = 0;
					VOID_RET(int, poll(fds, 1, 0));
#endif
					/* Read state of queue from mq fd */
					sret = read(mq, buffer, sizeof(buffer));
					if (sret < 0) {
						if (errno == EINTR)
							break;
						pr_fail("%s: mq read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
#endif
					(void)shim_memset(&sigev, 0, sizeof sigev);
					switch (stress_mwc8modn(5)) {
					case 0:
						/* Illegal signal */
						sigev.sigev_notify = SIGEV_SIGNAL;
						sigev.sigev_signo = ~0;
						break;
					case 1:
						/* Illegal notify event */
						sigev.sigev_notify = ~0;
						break;
					case 2:
						sigev.sigev_notify = SIGEV_NONE;
						break;
					case 3:
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

					/* Exercise illegal mq descriptor */
					(void)mq_notify(~0, &sigev);
					(void)mq_notify(0, &sigev);

					/* Exercise illegal mq_open name */
					attr.mq_flags = 0;
					attr.mq_maxmsg = sz;
					attr.mq_msgsize = sizeof(stress_msg_t);
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open("/bad/bad/bad", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					/* Exercise illegal mq_maxmsg */
					attr.mq_flags = 0;
					attr.mq_maxmsg = ~0;
					attr.mq_msgsize = sizeof(stress_msg_t);
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					attr.mq_flags = 0;
					attr.mq_maxmsg = LONG_MAX;
					attr.mq_msgsize = sizeof(stress_msg_t);
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					attr.mq_flags = 0;
					attr.mq_maxmsg = 0;
					attr.mq_msgsize = sizeof(stress_msg_t);
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					/* Exercise illegal mq_msgsize */
					attr.mq_flags = 0;
					attr.mq_maxmsg = sz;
					attr.mq_msgsize = ~0;
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					attr.mq_flags = 0;
					attr.mq_maxmsg = sz;
					attr.mq_msgsize = LONG_MAX;
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					attr.mq_flags = 0;
					attr.mq_maxmsg = sz;
					attr.mq_msgsize = 0;
					attr.mq_curmsgs = 0;
					stress_mq_invalid_open(mq_tmp_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

					/* Exercise invalid mq_unlink */
					(void)mq_unlink(mq_tmp_name);
					(void)mq_unlink("/bad/bad/bad");
					(void)mq_unlink("");
				}

				/*
				 * toggle between timedreceive and receive
				 */
				if (do_timed && (timed))
					sret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), &prio, &abs_timeout);
				else
					sret = mq_receive(mq, (char *)&msg, sizeof(msg), &prio);
				if (sret < 0) {
					if ((errno != EINTR) && (errno != ETIMEDOUT)) {
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

				if (do_timed && (timed)) {
					/* Exercise mq_timedreceive on invalid mq descriptors */
					(void)mq_timedreceive(-1, (char *)&msg, sizeof(msg), &prio, &abs_timeout);
					(void)mq_timedreceive(0, (char *)&msg, sizeof(msg), &prio, &abs_timeout);

					/* Exercise mq_timedreceive on invalid mq size */
					(void)mq_timedreceive(mq, (char *)&msg, (size_t)0, &prio, &abs_timeout);
				} else {
					/* Exercise mq_receive on invalid mq descriptors */
					(void)mq_receive(-1, (char *)&msg, sizeof(msg), &prio);
					(void)mq_receive(0, (char *)&msg, sizeof(msg), &prio);

					/* Exercise mq_receive on invalid mq size */
					(void)mq_receive(mq, (char *)&msg, (size_t)0, &prio);
				}
			}
			_exit(EXIT_SUCCESS);
		}
	} else {
		int attr_count = 0;
		stress_msg_t ALIGN64 msg;
		uint64_t values[PRIOS_MAX];
		uint64_t i = 0;

		/* Parent */
		(void)shim_memset(&msg, 0, sizeof(msg));
		(void)shim_memset(&values, 0, sizeof(values));

		do {
			int ret;
			unsigned int prio = stress_mwc8modn_maybe_pwr2(PRIOS_MAX);
			const uint64_t timed = (msg.value & 1);

			if ((attr_count++ & 31) == 0) {
				struct mq_attr old_attr;

				if (mq_getattr(mq, &attr) < 0) {
					pr_fail("%s: mq_getattr failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				} else {
					(void)mq_setattr(mq, &attr, &old_attr);
				}

				/* Exercise invalid mq id */
				if ((mq_getattr(~0, &attr) < 0) && (errno == EBADF))
					(void)mq_setattr(~0, &attr, &old_attr);

				/* Exercise invalid mq id */
				if ((mq_getattr(0, &attr) == EBADF) && (errno == EBADF))
					(void)mq_setattr(0, &attr, &old_attr);
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

			if (!(i & 1023)) {
				if (do_timed && (timed)) {
					/* Exercise mq_timedsend on invalid mq descriptors */
					(void)mq_timedsend(-1, (char *)&msg, sizeof(msg), prio, &abs_timeout);
					(void)mq_timedsend(0, (char *)&msg, sizeof(msg), prio, &abs_timeout);

					/* Exercise mq_timedsend on invalid mq size */
					(void)mq_timedsend(mq, (char *)&msg, ~(size_t)0, prio, &abs_timeout);

					/* Exercise mq_timedsend on invalid priority size */
					(void)mq_timedsend(mq, (char *)&msg, sizeof(msg), max_prio, &abs_timeout);
				} else {
					/* Exercise mq_send on invalid mq descriptors */
					(void)mq_send(-1, (char *)&msg, sizeof(msg), prio);
					(void)mq_send(0, (char *)&msg, sizeof(msg), prio);

					/* Exercise mq_send on invalid mq size */
					(void)mq_send(mq, (char *)&msg, ~(size_t)0, prio);

					/* Exercise mq_send on invalid priority size */
					(void)mq_send(mq, (char *)&msg, sizeof(msg), max_prio);
				}
				(void)mq_close(-1);
			}
			i++;

			stress_bogo_inc(args);
		} while (stress_continue(args));

		(void)stress_kill_pid_wait(pid, NULL);
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
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mq_info = {
	.stressor = stress_mq,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_mq_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without mqueue.h, POSIX message queues or librt support"
};
#endif
