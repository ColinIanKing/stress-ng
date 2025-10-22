/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"

#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#endif

#if defined(HAVE_SYS_MSG_H)
#include <sys/msg.h>
#endif

#define MIN_MSG_BYTES		(4)
#define MAX_MSG_BYTES		(8192)

#define STRESS_MAX_IDS		(1024)

static const stress_help_t help[] = {
	{ NULL,	"msg N",	"start N workers stressing System V messages" },
	{ NULL,	"msg-ops N",	"stop msg workers after N bogo messages" },
	{ NULL, "msg-types N",	"enable N different message types" },
	{ NULL, "msg-bytes N",	"set the message size 4..8192" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_msg_types, "msg-types", TYPE_ID_INT32, 0, 100, NULL },
	{ OPT_msg_bytes, "msg-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_MSG_BYTES, MAX_MSG_BYTES, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_MSG_H) &&	\
    defined(HAVE_MQ_SYSV)

typedef struct {
	long int mtype;
	union {
		uint32_t value;
		char data[MAX_MSG_BYTES];
	} u;
} stress_msg_t;

static int stress_msg_get_stats(stress_args_t *args, const int msgq_id)
{
#if defined(IPC_STAT)
	{
		struct msqid_ds buf;

		if (UNLIKELY(msgctl(msgq_id, IPC_STAT, &buf) < 0)) {
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
		(void)shim_memset(&buf, 0, sizeof(buf));

		/*
		 * select random msgq index numbers, we may hit
		 * some that are in use. Ignore failures
		 */
		(void)msgctl(stress_mwc8modn(msgq_id + 1), MSG_STAT_ANY, &buf);
	}
#endif

#if defined(IPC_INFO) &&	\
    defined(HAVE_MSGINFO)
	{
		struct msginfo info;

		if (UNLIKELY(msgctl(msgq_id, IPC_INFO, (struct msqid_ds *)&info) < 0)) {
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

		if (UNLIKELY(msgctl(msgq_id, MSG_INFO, (struct msqid_ds *)&info) < 0)) {
			pr_fail("%s: msgctl MSG_INFO failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -errno;
		}
	}
#endif
	{
		struct msqid_ds buf;

		/* Keep static analyzers happy */
		(void)shim_memset(&buf, 0, sizeof(buf));

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
	if (UNLIKELY(msgq_id >= 0))
		(void)msgctl(msgq_id, IPC_RMID, NULL);

	/* All flags, probably succeeds */
	msgq_id = msgget(IPC_CREAT, ~0);
	if (UNLIKELY(msgq_id >= 0))
		(void)msgctl(msgq_id, IPC_RMID, NULL);
}


/*
 *  stress_msgsnd()
 *	exercise msgsnd with some more unusual arguments
 */
static void stress_msgsnd(const int msgq_id, const size_t msg_bytes)
{
	stress_msg_t msg ALIGN64;

	/* Invalid msgq_id */
	msg.mtype = 0;
	msg.u.value = 0;
	VOID_RET(int, msgsnd(-1, &msg, msg_bytes, 0));

	/* Zero msg length + 0 msg.type */
	msg.mtype = 0;
	VOID_RET(int, msgsnd(msgq_id, &msg, 0, 0));

	/* Illegal flags, may or may not succeed */
	msg.mtype = 0;
	VOID_RET(int, msgsnd(msgq_id, &msg, msg_bytes, ~0));
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
	if (UNLIKELY(fd < 0)) {
		*get_procinfo = false;
		return;
	}
	for (;;) {
		char buffer[4096] ALIGN64;

		if (read(fd, buffer, sizeof(buffer)) <= 0)
			break;
	}
	(void)close(fd);
}
#endif

/*
 *  Set upper/lower limits on maximum msgq ids to be allocated
 */
static inline size_t stress_max_ids(stress_args_t *args)
{
	size_t max_ids;

	/* Avoid static analysis complaining about division errors */
	if (args->instances < 1)
		return STRESS_MAX_IDS;

	max_ids = STRESS_MAX_IDS / args->instances;
	if (max_ids < 2)
		return 2;
	return max_ids;
}

static int OPTIMIZE3 stress_msg_receiver(
	stress_args_t *args,
	const int msgq_id,
	const int32_t msg_types,
	const size_t msg_bytes)
{
	stress_msg_t ALIGN64 msg;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	while (stress_continue(args)) {
		register uint32_t i;
		register const long int mtype = msg_types == 0 ? 0 : -(msg_types + 1);

		for (i = 0; LIKELY(stress_continue(args)); i++) {
#if defined(IPC_NOWAIT)
			int msg_flag = (i & 0x1ff) ? 0 : IPC_NOWAIT;
#else
			int msg_flag = 0;
#endif
			ssize_t msgsz;

#if defined(MSG_COPY) &&	\
    defined(IPC_NOWAIT)
			/*
			 *  Very occasionally peek with a MSG_COPY, ignore
			 *  the return as we just want to exercise the flag
			 *  and we don't care if it succeeds or not
			 */
			if (UNLIKELY((i & 0xfff) == 0)) {
				VOID_RET(ssize_t, msgrcv(msgq_id, &msg, msg_bytes, mtype,
					MSG_COPY | IPC_NOWAIT));
			}
#endif
			if (UNLIKELY((i & 0x1ff) == 0)) {
				/* Exercise invalid msgrcv queue ID */
				(void)msgrcv(-1, &msg, msg_bytes, mtype, 0);
			}

redo:
			msgsz = msgrcv(msgq_id, &msg, msg_bytes, mtype, msg_flag);
			if (UNLIKELY(msgsz < 0)) {
				/*
				 * Check for errors that can occur
				 * when the termination occurs and
				 * retry
				 */
				if (LIKELY((errno == ENOMSG) || (errno == EAGAIN))) {
					msg_flag = 0;
					goto redo;
				}
				if (LIKELY((errno == E2BIG) || (errno == EINTR)))
					continue;

				pr_fail("%s: msgrcv failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			/*  Short data in message, bail out */
			if (UNLIKELY(msgsz < (ssize_t)sizeof(msg.u.value)))
				break;
			/*
			 *  Only when msg_types is not set can we fetch
			 *  data in an ordered FIFO to sanity check data
			 *  ordering.
			 */
			if (UNLIKELY(verify && (msg_types == 0))) {
				if (UNLIKELY(msg.u.value != i)) {
					pr_fail("%s: msgrcv: expected msg containing 0x%" PRIx32
						" but received 0x%" PRIx32 " instead (data length %zd)\n",
						 args->name, i, msg.u.value, msgsz);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
	}
	return rc;
}

static void OPTIMIZE3 stress_msg_sender(
	stress_args_t *args,
	const int msgq_id,
	const int32_t msg_types,
	const size_t msg_bytes)
{
	stress_msg_t ALIGN64 msg;
#if defined(__linux__)
	bool get_procinfo = true;
#endif

	/* Parent */
	(void)shim_memset(&msg.u.data, '#', sizeof(msg.u.data));
	msg.u.value = 0;

	do {
#if defined(IPC_NOWAIT)
		int msg_flag = (msg.u.value & 0x3f) ? 0 : IPC_NOWAIT;
#else
		int msg_flag = 0;
#endif

		msg.mtype = (msg_types) ? stress_mwc8modn(msg_types) + 1 : 1;
resend:
		if (UNLIKELY(msgsnd(msgq_id, &msg, msg_bytes, msg_flag) < 0)) {
			if (errno == EAGAIN) {
				msg_flag = 0;
				goto resend;
			}
			if (errno != EINTR)
				pr_fail("%s: msgsnd failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		}
		msg.u.value++;
		stress_bogo_inc(args);
		if (UNLIKELY((msg.u.value & 0xff) == 0)) {
			if (UNLIKELY(stress_msg_get_stats(args, msgq_id) < 0))
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

#if defined(__linux__)
			/*
			 *  Periodically read /proc/sysvipc/msg to exercise
			 *  this interface if it exists
			 */
			if (UNLIKELY(get_procinfo && ((msg.u.value & 0xffff) == 0)))
				stress_msg_get_procinfo(&get_procinfo);
#endif
		}
	} while (stress_continue(args));

	stress_msgsnd(msgq_id, msg_bytes);

}

/*
 *  stress_msg
 *	stress by message queues
 */
static int stress_msg(stress_args_t *args)
{
	pid_t pid;
	int msgq_id, rc = EXIT_SUCCESS, parent_cpu;
	int32_t msg_types = 0;
	const size_t max_ids = stress_max_ids(args);
	int *msgq_ids;
	stress_msg_t ALIGN64 msg;
	size_t j, n, msg_bytes = sizeof(msg.u.value);

	(void)stress_get_setting("msg-types", &msg_types);
	if (!stress_get_setting("msg-bytes", &msg_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			msg_bytes = MAX_MSG_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			msg_bytes = MIN_MSG_BYTES;
	}

	msgq_ids = (int *)calloc(max_ids, sizeof(*msgq_ids));
	if (!msgq_ids) {
		pr_inf_skip("%s: failed to allocate %zu item msgq id array%s, "
			"skipping stressor\n", args->name, max_ids,
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0) {
		const int ret = stress_exit_status(errno);

		if (ret == EXIT_FAILURE) {
			pr_fail("%s: msgget failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
			pr_inf_skip("%s: msgget out of resources or not implemented, skipping stressor\n", args->name);
		}
		free(msgq_ids);
		return ret;
	}
	pr_dbg("%s: System V message queue created, id: %d\n", args->name, msgq_id);

	stress_msgget();
	for (n = 0; n < max_ids; n++) {
		if (UNLIKELY(!stress_continue(args)))
			break;
		msgq_ids[n] = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
		if ((msgq_ids[n] < 0) &&
		    ((errno == ENOMEM) || (errno == ENOSPC)))
			break;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(!stress_continue(args)))
		goto cleanup;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto cleanup;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto cleanup;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		rc = stress_msg_receiver(args, msgq_id, msg_types, msg_bytes);
		_exit(rc);
	} else {
		stress_msg_sender(args, msgq_id, msg_types, msg_bytes);
		rc = stress_kill_and_wait(args, pid, SIGKILL, false);

		if (msgctl(msgq_id, IPC_RMID, NULL) < 0) {
			pr_fail("%s: msgctl failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		else
			pr_dbg("%s: System V message queue deleted, id: %d\n", args->name, msgq_id);
	}

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (j = 0; j < n; j++) {
		if (msgq_ids[j] >= 0)
			(void)msgctl(msgq_ids[j], IPC_RMID, NULL);
	}
	free(msgq_ids);

	return rc;
}

const stressor_info_t stress_msg_info = {
	.stressor = stress_msg,
	.classifier = CLASS_SCHEDULER | CLASS_OS | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_msg_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/ipc.h, sys/msg.h or System V message queues support"
};
#endif
