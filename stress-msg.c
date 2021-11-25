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

#define STRESS_MAX_IDS		(1024)

static const stress_help_t help[] = {
	{ NULL,	"msg N",	"start N workers stressing System V messages" },
	{ NULL,	"msg-ops N",	"stop msg workers after N bogo messages" },
	{ NULL, "msg-types N",	"enable N different message types" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_msg_types(const char *opt) {
	int32_t msg_types;

	msg_types = stress_get_int32(opt);
	stress_check_range("msg-types", (uint64_t)msg_types, 0, 100);
	return stress_set_setting("msg-types", TYPE_ID_INT32, &msg_types);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_msg_types,	stress_set_msg_types },
	{ 0,                    NULL },
};

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H) &&	\
    defined(HAVE_MQ_SYSV)

typedef struct {
	long mtype;
	uint32_t value;
} stress_msg_t;

static int stress_msg_get_stats(const stress_args_t *args, const int msgq_id)
{
#if defined(IPC_STAT)
	{
		struct msqid_ds buf;

		if (msgctl(msgq_id, IPC_STAT, &buf) < 0) {
			pr_fail("%s: msgctl IPC_STAT failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -errno;
		}

#if defined(IPC_SET)
		/* exercise IPC_SET, ignore failures */
		(void)msgctl(msgq_id, IPC_SET, &buf);
#endif
	}
#endif

#if defined(MSG_STAT_ANY)
	{
		struct msqid_ds buf;

		/* Keep static analyzers happy */
		(void)memset(&buf, 0, sizeof(buf));

		/*
		 * select random msgq index numbers, we may hit
		 * some that are in use. Ignore failures
		 */
		(void)msgctl(stress_mwc8() % (msgq_id + 1), MSG_STAT_ANY, &buf);
	}
#endif

#if defined(IPC_INFO) &&	\
    defined(HAVE_MSGINFO)
	{
		struct msginfo info;

		if (msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info) < 0) {
			pr_fail("%s: msgctl IPC_INFO failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -errno;
		}
	}
#endif
#if defined(MSG_INFO) &&	\
    defined(HAVE_MSGINFO)
	{
		struct msginfo info;

		if (msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info) < 0) {
			pr_fail("%s: msgctl MSG_INFO failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -errno;
		}
	}
#endif
	{
		struct msqid_ds buf;

		/* Keep static analyzers happy */
		(void)memset(&buf, 0, sizeof(buf));

		/* Exercise invalid msgctl commands */
		(void)msgctl(msgq_id, ~0, &buf);
		(void)msgctl(msgq_id, 0xffff, &buf);

		/* Exercise invalid msgq_ids */
		(void)msgctl(-1, IPC_STAT, &buf);
		(void)msgctl(msgq_id | 0x7f000000, IPC_STAT, &buf);
	}

	return 0;
}

/*
 *  stress_msgget()
 *	exercise msgget with some more unusual arguments
 */
static void stress_msgget(void)
{
	int msgq_id;

	/* Illegal key */
	msgq_id = msgget(-1, S_IRUSR | S_IWUSR);
	if (msgq_id >= 0)
		(void)msgctl(msgq_id, IPC_RMID, NULL);

	/* All flags, probably succeeds */
	msgq_id = msgget(IPC_CREAT, ~0);
	if (msgq_id >= 0)
		(void)msgctl(msgq_id, IPC_RMID, NULL);
}


/*
 *  stress_msgsnd()
 *	exercise msgsnd with some more unusual arguments
 */
static void stress_msgsnd(const int msgq_id)
{
	int ret;
	stress_msg_t msg;

	/* Invalid msgq_id */
	msg.mtype = 1;
	msg.value = 0;
	ret = msgsnd(-1, &msg, sizeof(msg.value), 0);
	(void)ret;

	/* Zero msg length + 0 msg.type */
	msg.mtype = 0;
	ret = msgsnd(msgq_id, &msg, 0, 0);
	(void)ret;

	/* Illegal flags, may or may not succeed */
	msg.mtype = 1;
	ret = msgsnd(msgq_id, &msg, sizeof(msg.value), ~0);
	(void)ret;
}

#if defined(__linux__)
/*
 *  stress_msg_get_procinfo()
 *	exercise /proc/sysvipc/msg
 */
static void stress_msg_get_procinfo(bool *get_procinfo)
{
	int fd;

	fd = open("/proc/sysvipc/msg", O_RDONLY);
	if (fd < 0) {
		*get_procinfo = false;
		return;
	}
	for (;;) {
		ssize_t ret;
		char buffer[1024];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret <= 0)
			break;
	}
	(void)close(fd);
}
#endif

/*
 *  Set upper/lower limits on maximum msgq ids to be allocated
 */
static inline size_t stress_max_ids(const stress_args_t *args)
{
	size_t max_ids;

	/* Avoid static analysis complaining about division errors */
	if (args->num_instances < 1)
		return STRESS_MAX_IDS;

	max_ids = STRESS_MAX_IDS / args->num_instances;
	if (max_ids < 2)
		return 2;
	return max_ids;
}

/*
 *  stress_msg
 *	stress by message queues
 */
static int stress_msg(const stress_args_t *args)
{
	pid_t pid;
	int msgq_id, rc = EXIT_SUCCESS;
	int32_t msg_types = 0;
#if defined(__linux__)
	bool get_procinfo = true;
#endif
	const size_t max_ids = stress_max_ids(args);
	int *msgq_ids;
	size_t j, n;

	(void)stress_get_setting("msg-types", &msg_types);

	msgq_ids = calloc(max_ids, sizeof(*msgq_ids));
	if (!msgq_ids) {
		pr_inf("%s: failed to allocate msgq id array\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0) {
		const int ret = exit_status(errno);

		pr_fail("%s: msgget failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		free(msgq_ids);
		return ret;
	}
	pr_dbg("%s: System V message queue created, id: %d\n", args->name, msgq_id);

	stress_msgget();
	for (n = 0; n < max_ids; n++) {
		if (!keep_stressing(args))
			break;
		msgq_ids[n] = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
		if ((msgq_ids[n] < 0) &&
		    ((errno == ENOMEM) || (errno == ENOSPC)))
			break;
	}
	inc_counter(args);

	if (!keep_stressing(args))
		goto cleanup;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		if (!keep_stressing(args))
			goto cleanup;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto cleanup;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		while (keep_stressing(args)) {
			stress_msg_t ALIGN64 msg;
			register uint32_t i;
			register const long mtype = msg_types == 0 ? 0 : -(msg_types + 1);

			for (i = 0; keep_stressing(args); i++) {
#if defined(MSG_COPY) &&	\
    defined(IPC_NOWAIT)
				/*
				 *  Very occasionally peek with a MSG_COPY, ignore
				 *  the return as we just want to exercise the flag
				 *  and we don't care if it succeeds or not
				 */
				if ((i & 0xfff) == 0) {
					ssize_t ret;

					ret = msgrcv(msgq_id, &msg, sizeof(msg.value), mtype,
						MSG_COPY | IPC_NOWAIT);
					(void)ret;
				}
#endif

				if ((i & 0x1ff) == 0) {
					/* Exercise invalid msgrcv queue ID */
					(void)msgrcv(-1, &msg, sizeof(msg.value), mtype, 0);

					/* Exercise invalid msgrcv message size */
					(void)msgrcv(msgq_id, &msg, -1, mtype, 0);
					(void)msgrcv(msgq_id, &msg, 0, mtype, 0);

					/* Exercise invalid msgrcv message flag */
					(void)msgrcv(msgq_id, &msg, sizeof(msg.value), mtype, ~0);
				}

				if (msgrcv(msgq_id, &msg, sizeof(msg.value), mtype, 0) < 0) {
					/*
					 * Check for errors that can occur
					 * when the termination occurs and
					 * retry
					 */
					if ((errno == E2BIG) || (errno == EINTR))
						continue;

					pr_fail("%s: msgrcv failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
				/*
				 *  Only when msg_types is not set can we fetch
				 *  data in an ordered FIFO to sanity check data
				 *  ordering.
				 */
				if ((msg_types == 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if (msg.value != i)
						pr_fail("%s: msgrcv: expected msg containing 0x%" PRIx32
							" but received 0x%" PRIx32 " instead\n",
							 args->name, i, msg.value);
				}
			}
			_exit(EXIT_SUCCESS);
		}
	} else {
		stress_msg_t ALIGN64 msg;
		int status;

		/* Parent */
		(void)setpgid(pid, g_pgrp);

		msg.value = 0;

		do {
			msg.mtype = (msg_types) ? (stress_mwc8() % msg_types) + 1 : 1;
			if (msgsnd(msgq_id, &msg, sizeof(msg.value), 0) < 0) {
				if (errno != EINTR)
					pr_fail("%s: msgsnd failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				break;
			}
			msg.value++;
			inc_counter(args);
			if ((msg.value & 0xff) == 0) {
				if (stress_msg_get_stats(args, msgq_id) < 0)
					break;
#if defined(__NetBSD__)
				/*
				 *  NetBSD can shove loads of messages onto
				 *  a queue before it blocks, so force
				 *  a scheduling yield every so often so that
				 *  consumer can read them.
				 */
				(void)shim_sched_yield();
#endif
			}

#if defined(__linux__)
			/*
			 *  Periodically read /proc/sysvipc/msg to exercise
			 *  this interface if it exists
			 */
			if (get_procinfo && ((msg.value & 0xffff) == 0))
				stress_msg_get_procinfo(&get_procinfo);
#endif

		} while (keep_stressing(args));

		stress_msgsnd(msgq_id);

		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);

		if (msgctl(msgq_id, IPC_RMID, NULL) < 0)
			pr_fail("%s: msgctl failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		else
			pr_dbg("%s: System V message queue deleted, id: %d\n", args->name, msgq_id);
	}

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (j = 0; j < n; j++) {
		if (msgq_ids[j] >= 0)
			msgctl(msgq_ids[j], IPC_RMID, NULL);
	}
	free(msgq_ids);

	return rc;
}

stressor_info_t stress_msg_info = {
	.stressor = stress_msg,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_msg_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
