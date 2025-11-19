/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "core-asm-arm.h"
#include "core-asm-loong64.h"
#include "core-asm-ppc64.h"
#include "core-asm-riscv.h"
#include "core-asm-x86.h"
#include "core-asm-generic.h"
#include "core-cpu-cache.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-sched.h"
#include "core-target-clones.h"
#include "core-vecmath.h"
#include "core-workload.h"

#include <math.h>
#include <sched.h>
#include <time.h>


#define STRESS_VARYLOAD_TYPE_SAW_INC	(0)
#define STRESS_VARYLOAD_TYPE_SAW_DEC	(1)
#define STRESS_VARYLOAD_TYPE_TRIANGLE	(2)
#define STRESS_VARYLOAD_TYPE_PULSE	(3)
#define STRESS_VARYLOAD_TYPE_RANDOM 	(4)

#define STRESS_VARYLOAD_TYPE_DEFAULT	STRESS_VARYLOAD_TYPE_TRIANGLE
#define STRESS_VARYLOAD_MS_DEFAULT	(1000)	/* 1 second */

#define SCHED_UNDEFINED	(-1)

typedef struct {
	const char *name;
	const int type;
} stress_varyload_type_t;

typedef struct {
	const char *name;
	const int method;
} stress_varyload_method_t;

static const stress_help_t help[] = {
	{ NULL,	"varyload N",		"start N workers that exercise a mix of scheduling loads" },
	{ NULL,	"varyload-ops N",	"stop after N varyload bogo operations" },
	{ NULL, "varyload-ms M",	"vary workload every M milliseconds" },
	{ NULL, "varyload-sched P",	"select scheduler policy [ batch | deadline | ext | fifo | idle | rr | other ]" },
	{ NULL, "varyload-method M",	"select a varyload method, default is all" },
	{ NULL, "varyload-type T",	"select a varyload load type [ saw-inc, saw-dec | trigangle | pulse | random ]" },
	{ NULL,	NULL,			NULL }
};

static const stress_varyload_type_t varyload_types[] = {
	{ "saw-inc",	STRESS_VARYLOAD_TYPE_SAW_INC },
	{ "saw-dec",	STRESS_VARYLOAD_TYPE_SAW_DEC },
	{ "triangle",	STRESS_VARYLOAD_TYPE_TRIANGLE },
	{ "pulse",	STRESS_VARYLOAD_TYPE_PULSE },
	{ "random",	STRESS_VARYLOAD_TYPE_RANDOM },
};

static int fds[2];

static void stress_varyload_init(const uint32_t instances)
{
	(void)instances;

	if (pipe(fds) < 0) {
		fds[0] = -1;
		fds[1] = -1;
	}
}

static void stress_varyload_deinit(void)
{
	if (fds[0] >= 0)
		(void)close(fds[0]);
	if (fds[1] >= 0)
		(void)close(fds[1]);
}

static const char *stress_varyload_type(const size_t i)
{
	return (i < SIZEOF_ARRAY(varyload_types)) ? varyload_types[i].name : NULL;
}

static const char *stress_varyload_sched(const size_t i)
{
	return (i < stress_sched_types_length) ? stress_sched_types[i].sched_name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_varyload_method,	"varyload-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_workload_method },
	{ OPT_varyload_ms,	"varyload-ms",	   TYPE_ID_UINT32, 1, 36000000, NULL },
	{ OPT_varyload_sched,	"varyload-sched",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_varyload_sched },
	{ OPT_varyload_type,	"varyload-type",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_varyload_type },
	END_OPT,
};

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
static int stress_varyload_set_sched(
	stress_args_t *args,
	const size_t varyload_sched)
{
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	struct shim_sched_attr attr;
#else
	UNEXPECTED
#endif
	struct sched_param param;
	int ret = 0;
	int max_prio, min_prio, rng_prio;
	const pid_t pid = getpid();
	const char *policy_name;
	int policy;

	if ((varyload_sched < 1) || (varyload_sched >= stress_sched_types_length))
		return 0;

	policy_name = stress_sched_types[varyload_sched].sched_name;
	policy = stress_sched_types[varyload_sched].sched;

	errno = 0;
	switch (policy) {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	case SCHED_DEADLINE:
		(void)shim_memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		attr.sched_flags = 0;
		attr.sched_nice = 0;
		attr.sched_priority = 0;
		attr.sched_policy = SCHED_DEADLINE;
		/* runtime <= deadline <= period */
		attr.sched_runtime = 64 * 1000000;
		attr.sched_deadline = 128 * 1000000;
		attr.sched_period = 256 * 1000000;

		ret = shim_sched_setattr(0, &attr, 0);
		break;
#endif
#if defined(SCHED_EXT)
	case SCHED_EXT:
#endif
#if defined(SCHED_BATCH)
	case SCHED_BATCH:
#endif
#if defined(SCHED_IDLE)
	case SCHED_IDLE:
#endif
#if defined(SCHED_OTHER)
	case SCHED_OTHER:
#endif
		param.sched_priority = 0;
		ret = sched_setscheduler(pid, policy, &param);

		break;
#if defined(SCHED_RR)
	case SCHED_RR:
#if defined(HAVE_SCHED_RR_GET_INTERVAL)
		{
			struct timespec t;

			VOID_RET(int, sched_rr_get_interval(pid, &t));
		}
#endif
		goto case_sched_fifo;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
case_sched_fifo:
		min_prio = sched_get_priority_min(policy);
		max_prio = sched_get_priority_max(policy);

		/* Check if min/max is supported or not */
		if ((min_prio == -1) || (max_prio == -1)) {
			pr_inf("%s: cannot get min/max priority levels, not setting scheduler policy\n",
				args->name);
		}

		rng_prio = max_prio - min_prio;
		if (UNLIKELY(rng_prio == 0)) {
			pr_err("%s: invalid min/max priority "
				"range for scheduling policy %s "
				"(min=%d, max=%d)\n",
				args->name,
				policy_name,
				min_prio, max_prio);
			break;
		}
		param.sched_priority = (int)stress_mwc32modn(rng_prio) + min_prio;
		ret = sched_setscheduler(pid, policy, &param);
		break;
	default:
		/* Should never get here */
		break;
	}

	if (ret < 0) {
		if (errno == EPERM) {
			if (stress_instance_zero(args))
				pr_inf("%s: insufficient privilege to set scheduler to '%s'\n",
					args->name, policy_name);
			return 0;
		}
		/*
		 *  Some systems return EINVAL for non-POSIX
		 *  scheduling policies, silently ignore these
		 *  failures.
		 */
		pr_inf("%s: sched_setscheduler "
			"failed, errno=%d (%s) "
			"for scheduler policy %s\n",
			args->name, errno, strerror(errno),
			policy_name);
	} else {
		if (stress_instance_zero(args))
			pr_inf("%s: using '%s' scheduler\n",
				args->name, policy_name);
	}
	return ret;
}
#else
static int stress_varyload_set_sched(
	stress_args_t *args,
	const size_t varyload_sched)
{
	(void)args;
	(void)varyload_sched;

	return 0;
}
#endif

/*
 *  stress_varyload_waste_time()
 *	waste time for varyload_ms milliseconds in
 *	millisecond batches
 */
static void stress_varyload_waste_time(
	stress_args_t *args,
	const int workload_method,
	const uint32_t varyload_ms,
	uint8_t *buffer,
	const size_t buffer_len)
{
	double t_end = stress_time_now() + (0.001 * (double)varyload_ms);

	do {
		stress_workload_waste_time(args->name, workload_method, 0.001, buffer, buffer_len);
		stress_bogo_inc(args);
	} while (stress_continue_flag() && (stress_time_now() < t_end));
}

/*
 *  stress_varyload
 *	load system with varying loads
 */
static int stress_varyload(stress_args_t *args)
{
	uint32_t varyload_ms = STRESS_VARYLOAD_MS_DEFAULT;
	size_t varyload_type_idx = STRESS_VARYLOAD_TYPE_DEFAULT;
	size_t varyload_sched = 0;		/* undefined */
	size_t varyload_method_idx = 0;		/* all */
	int varyload_method, varyload_type;
	uint8_t *buffer;
	const size_t buffer_len = MB;
	int rc = EXIT_SUCCESS;
	pid_t *pids;
	uint32_t i, load = 1;
	bool controller = stress_instance_zero(args);
	bool sync_fail = false;

	pids = calloc((size_t)args->instances, sizeof(*pids));
	if (!pids) {
		pr_inf("%s: failed to allocate %" PRIu32 " pids, skipping stressor\n",
			args->name, args->instances);
		return EXIT_NO_RESOURCE;
	}

	pids[0] = getpid();
	if (controller) {
		for (i = 1; i < args->instances; i++) {
			ssize_t len;

redo:
			if (!stress_continue(args))
				break;
			len = read(fds[0], &pids[i], sizeof(pid_t));
			if (len < (ssize_t)sizeof(pid_t)) {
				if (errno == EINTR)
					goto redo;
				sync_fail = true;
			}
		}
		if (sync_fail) {
			pr_inf("%s: pid_t %" PRIu32 " read error during process synchronisation, "
				"errno=%d (%s), skipping stressor\n",
				args->name, i, errno, strerror(errno));
			free(pids);
			return EXIT_NO_RESOURCE;
		}
	} else {
		ssize_t len;
		const pid_t pid = getpid();

		len = write(fds[1], &pid, sizeof(pid_t));
		if (len < (ssize_t)sizeof(pid_t)) {
			pr_inf_skip("%s: pid_t write error during process synchronisation, "
				"errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			free(pids);
			return EXIT_NO_RESOURCE;
		}
	}

	(void)stress_get_setting("varyload-method", &varyload_method_idx);
	(void)stress_get_setting("varyload-ms", &varyload_ms);
	(void)stress_get_setting("varyload-sched", &varyload_sched);
	(void)stress_get_setting("varyload-type", &varyload_type_idx);

	varyload_method = workload_methods[varyload_method_idx].method;
	varyload_type = varyload_types[varyload_type_idx].type;

	buffer = (uint8_t *)stress_mmap_populate(NULL, buffer_len,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, buffer_len,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto exit_free_pids;
	}
	(void)stress_madvise_nohugepage(buffer, buffer_len);
	stress_set_vma_anon_name(buffer, buffer_len, "varyload-buffer");

	(void)stress_varyload_set_sched(args, varyload_sched);

	if (stress_instance_zero(args)) {
		pr_inf("%s: using load method '%s', load type '%s', varying every %" PRIu32 "ms\n",
			args->name, workload_methods[varyload_method].name,
			varyload_types[varyload_type].name,
			varyload_ms);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (controller) {
		for (i = 1; i < args->instances; i++)
			kill(pids[i], SIGSTOP);

		switch (varyload_type) {
		case STRESS_VARYLOAD_TYPE_SAW_INC:
			do {
				for (i = 1; i < load; i++)
					(void)kill(pids[i], SIGCONT);
				for (; i < args->instances; i++)
					(void)kill(pids[i], SIGSTOP);
				if (!stress_continue(args))
					break;
				stress_varyload_waste_time(args, varyload_method,
					varyload_ms, buffer, buffer_len);
				load++;
				if (load >= args->instances)
					load = 1;
			} while (stress_continue(args));
			break;
		case STRESS_VARYLOAD_TYPE_SAW_DEC:
			load = args->instances;
			do {
				for (i = 1; i < load; i++)
					(void)kill(pids[i], SIGCONT);
				for (; i < args->instances; i++)
					(void)kill(pids[i], SIGSTOP);
				if (!stress_continue(args))
					break;
				stress_varyload_waste_time(args, varyload_method,
					varyload_ms, buffer, buffer_len);
				load--;
				if (load < 1)
					load = args->instances;
			} while (stress_continue(args));
			break;
		case STRESS_VARYLOAD_TYPE_TRIANGLE:
			do {
				for (load = 1; load < args->instances; load++) {
					for (i = 1; i < load; i++)
						(void)kill(pids[i], SIGCONT);
					for (; i < args->instances; i++)
						(void)kill(pids[i], SIGSTOP);
					if (!stress_continue(args))
						break;
					stress_varyload_waste_time(args, varyload_method,
						varyload_ms, buffer, buffer_len);
				}
				for (load = args->instances; load > 1; load--) {
					for (i = 1; i < load; i++)
						(void)kill(pids[i], SIGCONT);
					for (; i < args->instances; i++)
						(void)kill(pids[i], SIGSTOP);
					if (!stress_continue(args))
						break;
					stress_varyload_waste_time(args, varyload_method,
						varyload_ms, buffer, buffer_len);
				}
			} while (stress_continue(args));
			break;
		case STRESS_VARYLOAD_TYPE_PULSE:
			do {
				for (i = 1; i < args->instances; i++)
					(void)kill(pids[i], SIGSTOP);
				(void)shim_usleep_interruptible(varyload_ms * 1000);
				for (i = 1; i < args->instances; i++)
					(void)kill(pids[i], SIGCONT);
				if (!stress_continue(args))
					break;
				stress_varyload_waste_time(args, varyload_method,
					varyload_ms, buffer, buffer_len);
			} while (stress_continue(args));
			break;
		case STRESS_VARYLOAD_TYPE_RANDOM:
			do {
				for (i = 1; i < args->instances; i++) {
					(void)kill(pids[i], stress_mwc1() ? SIGSTOP : SIGCONT);
				}
				if (!stress_continue(args))
					break;
				if (stress_mwc1())
					(void)shim_usleep_interruptible(varyload_ms * 1000);
				else
					stress_varyload_waste_time(args, varyload_method,
						varyload_ms, buffer, buffer_len);
			} while (stress_continue(args));
			break;
		default:
			break;
		}

		for (i = 1; i < args->instances; i++)
			kill(pids[i], SIGCONT);
	} else {
		do {
			stress_varyload_waste_time(args, varyload_method,
				varyload_ms, buffer, buffer_len);
		} while (stress_continue(args));
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)buffer, buffer_len);
exit_free_pids:
	free(pids);

	return rc;
}

const stressor_info_t stress_varyload_info = {
	.stressor = stress_varyload,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.init = stress_varyload_init,
	.deinit = stress_varyload_deinit,
	.opts = opts,
	.help = help
};
