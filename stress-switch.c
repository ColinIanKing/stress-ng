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
#include "core-mmap.h"

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ "s N","switch N",	 	"start N workers doing rapid context switches" },
	{ NULL, "switch-freq N", 	"set frequency of context switches" },
	{ NULL, "switch-method M",	"mq | pipe | sem-sysv" },
	{ NULL,	"switch-ops N",	 	"stop after N context switch bogo operations" },
	{ NULL, NULL, 			NULL }
};

typedef int (*switch_func_t)(stress_args_t *args,
			     const uint64_t switch_freq,
			     const uint64_t switch_delay,
			     const uint64_t threshold);
typedef struct {
	const char *name;
	const switch_func_t switch_func;
} stress_switch_method_t;

#define THRESH_FREQ	(100)		/* Delay adjustment rate in HZ */

/*
 *  stress_switch_rate()
 *	report context switch duration
 */
static void stress_switch_rate(
	stress_args_t *args,
	const char *method,
	const double t_start,
	const double t_end,
	uint64_t counter)
{
	char msg[128];

	(void)snprintf(msg, sizeof(msg), "nanosecs per context switch (%s method)", method);
	stress_metrics_set(args, 0, msg,
		((t_end - t_start) * STRESS_NANOSECOND) / (double)counter,
		STRESS_METRIC_HARMONIC_MEAN);
}

/*
 *  stress_switch_delay()
 *	adjustable delay to try and keep switch rate to
 *	a specified frequency
 */
static void stress_switch_delay(
	stress_args_t *args,
	const uint64_t switch_delay,
	const uint64_t threshold,
	const double t_start,
	uint64_t *delay)
{
	static uint64_t i = 0;

	/*
	 *  Small delays take a while, so skip these
	 */
	if (*delay > 1000)
		(void)shim_nanosleep_uint64(*delay);

	/*
	 *  This is expensive, so only update the
	 *  delay infrequently (at THRESH_FREQ HZ)
	 */
	if (++i >= threshold) {
		double overrun, overrun_by, t;
		const uint64_t counter = stress_bogo_get(args);

		i = 0;
		t = t_start + ((double)(counter * switch_delay) / STRESS_NANOSECOND);
		overrun = (stress_time_now() - t) * (double)STRESS_NANOSECOND;
		overrun_by = (double)switch_delay - overrun;

		if (overrun_by < 0.0) {
			/* Massive overrun, skip a delay */
			*delay = 0;
		} else {
			/* Overrun or underrun? */
			*delay = (uint64_t)overrun_by;
			if (*delay > switch_delay) {
				/* Don't delay more than the switch delay */
				*delay = switch_delay;
			}
		}
	}
}

/*
 *  stress_switch_pipe
 *	stress by heavy context switching using pipe
 *	synchronization method
 */
static int stress_switch_pipe(
	stress_args_t *args,
	const uint64_t switch_freq,
	const uint64_t switch_delay,
	const uint64_t threshold)
{
	pid_t pid;
	int pipefds[2], parent_cpu;
	size_t buf_size;
	char *buf;

	if (stress_sig_stop_stressing(args->name, SIGPIPE) < 0)
		return EXIT_FAILURE;

	(void)shim_memset(pipefds, 0, sizeof(pipefds));
#if defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		/*
		 *  Fallback to pipe if pipe2 fails
		 */
		if (UNLIKELY(pipe(pipefds) < 0)) {
			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}
	buf_size = 1;
#else
	if (UNLIKELY(pipe(pipefds) < 0)) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	buf_size = args->page_size;
#endif

	buf = (char *)stress_mmap_populate(NULL, buf_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (UNLIKELY(buf == MAP_FAILED)) {
		pr_fail("%s: failed to mmap %zu byte pipe read/write buffer%s, errno=%d (%s)\n",
			args->name, buf_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		return EXIT_FAILURE;
	}
	stress_set_vma_anon_name(buf, buf_size, "pipe-io-buffer");

#if defined(F_SETPIPE_SZ)
	if (UNLIKELY(fcntl(pipefds[0], F_SETPIPE_SZ, buf_size) < 0)) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (UNLIKELY(fcntl(pipefds[1], F_SETPIPE_SZ, buf_size) < 0)) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)buf, buf_size);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		register const int fd = pipefds[0];

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)close(pipefds[1]);

		while (stress_continue_flag()) {
			ssize_t ret;

			ret = read(fd, buf, buf_size);
			if (UNLIKELY(ret <= 0)) {
				if (errno == 0)	/* ret == 0 case */
					break;
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno == EPIPE)
					break;
				pr_dbg("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
		(void)close(pipefds[0]);
		_exit(EXIT_SUCCESS);
	} else {
		double t_start;
		uint64_t delay = switch_delay;
		register const int fd = pipefds[1];

		/* Parent */
		(void)close(pipefds[0]);
		(void)shim_memset(buf, '_', buf_size);

		t_start = stress_time_now();
		do {
			ssize_t ret;

			stress_bogo_inc(args);

			ret = write(fd, buf, buf_size);
			if (UNLIKELY(ret <= 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno == EPIPE)
					break;
				if (errno) {
					pr_dbg("%s: write failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
				continue;
			}

			if (UNLIKELY(switch_freq))
				stress_switch_delay(args, switch_delay, threshold, t_start, &delay);
		} while (stress_continue(args));

		stress_switch_rate(args, "pipe", t_start, stress_time_now(), stress_bogo_get(args));

		(void)close(pipefds[1]);
		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)buf, buf_size);

	return EXIT_SUCCESS;
}

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_KEY_T)
/*
 *  stress_switch_sem_sysv
 *	stress by heavy context switching using semaphore
 *	synchronization method
 */
static int stress_switch_sem_sysv(
	stress_args_t *args,
	const uint64_t switch_freq,
	const uint64_t switch_delay,
	const uint64_t threshold)
{
	pid_t pid;
	int i, sem_id = -1, parent_cpu;

	for (i = 0; i < 100; i++) {
		key_t key_id = (key_t)stress_mwc16();

		sem_id = semget(key_id, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
		if (sem_id >= 0)
			break;
	}
	if (sem_id < 0) {
		pr_err("%s: semaphore init (SYSV) failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
                return EXIT_FAILURE;
        }

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		while (stress_continue_flag()) {
			struct sembuf sem ALIGN64;

			sem.sem_num = 0;
			sem.sem_op = -1;
			sem.sem_flg = SEM_UNDO;

			if (UNLIKELY(semop(sem_id, &sem, 1) < 0))
				break;

			sem.sem_num = 0;
			sem.sem_op = 1;
			sem.sem_flg = SEM_UNDO;

			if (UNLIKELY(semop(sem_id, &sem, 1) < 0))
				break;
		}
		_exit(EXIT_SUCCESS);
	} else {
		double t_start;
		uint64_t delay = switch_delay;
		struct sembuf sem ALIGN64;

		/* Parent */
		t_start = stress_time_now();
		do {
			stress_bogo_inc(args);

			sem.sem_num = 0;
			sem.sem_op = 1;
			sem.sem_flg = SEM_UNDO;

			if (UNLIKELY(semop(sem_id, &sem, 1) < 0))
				break;

			if (UNLIKELY(switch_freq))
				stress_switch_delay(args, switch_delay, threshold, t_start, &delay);

			if (UNLIKELY(!stress_continue(args)))
				break;
			sem.sem_num = 0;
			sem.sem_op = -1;
			sem.sem_flg = SEM_UNDO;

			if (UNLIKELY(semop(sem_id, &sem, 1) < 0))
				break;
		} while (stress_continue(args));

		stress_switch_rate(args, "sem-sysv", t_start, stress_time_now(), 2 * stress_bogo_get(args));

		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)semctl(sem_id, 0, IPC_RMID);

	return EXIT_SUCCESS;
}
#endif

#if defined(HAVE_MQUEUE_H) &&   \
    defined(HAVE_LIB_RT) &&     \
    defined(HAVE_MQ_POSIX)
/*
 *  stress_switch_mq
 *	stress by heavy context switching using message queue
 */
static int stress_switch_mq(
	stress_args_t *args,
	const uint64_t switch_freq,
	const uint64_t switch_delay,
	const uint64_t threshold)
{
	typedef struct {
		uint64_t        value;
	} stress_msg_t;

	pid_t pid;
	mqd_t mq;
	char mq_name[64];
	struct mq_attr attr;
	stress_msg_t msg;
	int parent_cpu;

	(void)snprintf(mq_name, sizeof(mq_name), "/%s-%" PRIdMAX "-%" PRIu32,
			args->name, (intmax_t)args->pid, args->instance);
	attr.mq_flags = 0;
	attr.mq_maxmsg = 1;
	attr.mq_msgsize = sizeof(stress_msg_t);
	attr.mq_curmsgs = 0;
	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		pr_err("%s: message queue open failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
                return EXIT_FAILURE;
	}

	(void)shim_memset(&msg, 0, sizeof(msg));
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		while (LIKELY(stress_continue_flag())) {
			msg.value++;
			if (UNLIKELY(mq_send(mq, (char *)&msg, sizeof(msg), 0) < 0))
				break;
		}
		_exit(EXIT_SUCCESS);
	} else {
		double t_start;
		uint64_t delay = switch_delay;

		/* Parent */
		t_start = stress_time_now();
		do {
			unsigned int prio;

			stress_bogo_inc(args);
			if (UNLIKELY(mq_receive(mq, (char *)&msg, sizeof(msg), &prio) < 0))
				break;

			if (UNLIKELY(switch_freq))
				stress_switch_delay(args, switch_delay, threshold, t_start, &delay);
		} while (stress_continue(args));

		stress_switch_rate(args, "mq", t_start, stress_time_now(), stress_bogo_get(args));

		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)mq_close(mq);
	(void)mq_unlink(mq_name);

	return EXIT_SUCCESS;
}
#endif

static const stress_switch_method_t stress_switch_methods[] = {
#if defined(HAVE_MQUEUE_H) &&   \
    defined(HAVE_LIB_RT) &&     \
    defined(HAVE_MQ_POSIX)
	{ "mq",		stress_switch_mq },
#endif
	{ "pipe",	stress_switch_pipe },
#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_KEY_T)
	{ "sem-sysv",	stress_switch_sem_sysv },
#endif
};

/*
 *  stress_switch
 *	stress by heavy context switching
 */
static int stress_switch(stress_args_t *args)
{
	uint64_t switch_freq = 0, switch_delay, threshold;
	size_t switch_method = 0, i;

	for (i = 0; i < SIZEOF_ARRAY(stress_switch_methods); i++) {
		if (strcmp(stress_switch_methods[i].name, "pipe") == 0) {
			switch_method = i;
			break;
		}
	}

	(void)stress_get_setting("switch-freq", &switch_freq);
	(void)stress_get_setting("switch-method", &switch_method);

	switch_delay = (switch_freq == 0) ? 0 : STRESS_NANOSECOND / switch_freq;
	threshold = switch_freq / THRESH_FREQ;

	return stress_switch_methods[switch_method].switch_func(args, switch_freq, switch_delay, threshold);
}

static const char *stress_switch_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_switch_methods)) ? stress_switch_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_switch_freq,   "switch-freq",   TYPE_ID_UINT64, 0, STRESS_NANOSECOND, NULL },
	{ OPT_switch_method, "switch-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_switch_method },
	END_OPT,
};

const stressor_info_t stress_switch_info = {
	.stressor = stress_switch,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
