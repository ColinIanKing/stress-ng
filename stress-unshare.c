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
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"unshare N",	 "start N workers exercising resource unsharing" },
	{ NULL,	"unshare-ops N", "stop after N bogo unshare operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_UNSHARE)

typedef struct {
	pid_t	pid;		/* Process ID */
	double	duration;	/* unshare duration time (secs) */
	double	count;		/* unshare count */
} stress_unshare_info_t;

#define MAX_PIDS	(32)

#if defined(CLONE_FS)		|| \
    defined(CLONE_FILES)	|| \
    defined(CLONE_NEWCGROUP)	|| \
    defined(CLONE_NEWIPC)	|| \
    defined(CLONE_NEWNET)	|| \
    defined(CLONE_NEWNS)	|| \
    defined(CLONE_NEWPID)	|| \
    defined(CLONE_NEWUSER)	|| \
    defined(CLONE_NEWUTS)	|| \
    defined(CLONE_SYSVSEM)	|| \
    defined(CLONE_THREAD)	|| \
    defined(CLONE_SIGHAND)	|| \
    defined(CLONE_VM)

#define UNSHARE(flags, duration, count, rc)	\
	check_unshare(args, flags, #flags, duration, count, rc)

static const int clone_flags[] = {
#if defined(CLONE_FS)
	CLONE_FS,
#endif
#if defined(CLONE_FILES)
	CLONE_FILES,
#endif
#if defined(CLONE_NEWCGROUP)
	CLONE_NEWCGROUP,
#endif
#if defined(CLONE_NEWIPC)
	CLONE_NEWIPC,
#endif
#if defined(CLONE_NEWNS)
	CLONE_NEWNS,
#endif
#if defined(CLONE_NEWPID)
	CLONE_NEWPID,
#endif
#if defined(CLONE_NEWUSER)
	CLONE_NEWUSER,
#endif
#if defined(CLONE_NEWUTS)
	CLONE_NEWUTS,
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM,
#endif
#if defined(CLONE_THREAD)
	CLONE_THREAD,
#endif
#if defined(CLONE_SIGHAND)
	CLONE_SIGHAND,
#endif
#if defined(CLONE_VM)
	CLONE_VM,
#endif
};

/*
 *  unshare with some error checking
 */
static void check_unshare(
	stress_args_t *args,
	const int flags,
	const char *flags_name,
	double *duration,
	double *count,
	int *rc)
{
	int ret;
	double t;

	t = stress_time_now();
	ret = shim_unshare(flags);
	if (UNLIKELY((ret < 0) &&
		     (errno != EPERM) &&
		     (errno != EACCES) &&
		     (errno != EINVAL) &&
		     (errno != ENOSPC))) {
		pr_fail("%s: unshare(%s) failed, errno=%d (%s)\n",
			args->name, flags_name,
			errno, strerror(errno));
		*rc = EXIT_FAILURE;
	} else {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
	}
}
#endif

/*
 *  enough_memory()
 *	returns true if we have enough memory, ensures
 *	we don't throttle back the unsharing because we
 *	get into deep swappiness
 */
static inline bool enough_memory(void)
{
	size_t shmall, freemem, totalmem, freeswap, totalswap;
	bool enough;

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

	enough = (freemem == 0) ? true : freemem > (8 * MB);

	return enough;
}

/*
 *  stress_unshare()
 *	stress resource unsharing
 */
static int stress_unshare(stress_args_t *args)
{
#if defined(CLONE_NEWNET)
	const uid_t euid = geteuid();
#endif
	int *clone_flag_perms, all_flags;
	size_t i, clone_flag_count;
	const size_t unshare_info_size = sizeof(stress_unshare_info_t) * MAX_PIDS;
	stress_unshare_info_t *unshare_info;
	double total_duration = 0.0, total_count = 0.0, rate;
	int rc = EXIT_SUCCESS;

	unshare_info = (stress_unshare_info_t *)stress_mmap_populate(NULL,
				unshare_info_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (unshare_info == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zu bytes for unshare metrics%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, unshare_info_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(unshare_info, unshare_info_size, "unshare-metrics");

	for (i = 0; i < MAX_PIDS; i++) {
		unshare_info[i].duration = 0.0;
		unshare_info[i].count = 0.0;
	}

	for (all_flags = 0, i = 0; i < SIZEOF_ARRAY(clone_flags); i++)
		all_flags |= clone_flags[i];

	clone_flag_count = stress_flag_permutation(all_flags, &clone_flag_perms);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t n;

		for (n = 0; n < MAX_PIDS; n++)
			unshare_info[i].pid = -1;

		for (n = 0; n < MAX_PIDS; n++) {
			static size_t idx;
			int clone_flag = 0;
			const bool do_flag_perm = stress_mwc1();

			if (UNLIKELY(!stress_continue_flag()))
				break;
			if (!enough_memory()) {
				/* memory too low, back off */
				(void)sleep(1);
				break;
			}

			if (do_flag_perm) {
				clone_flag = clone_flag_perms[idx];
				idx++;
				if (idx >= clone_flag_count)
					idx = 0;
			}

			unshare_info[n].pid = fork();
			if (unshare_info[n].pid < 0) {
				/* Out of resources for fork */
				if (errno == EAGAIN)
					break;
			} else if (unshare_info[n].pid == 0) {
				double *duration = &unshare_info[i].duration;
				double *count = &unshare_info[i].count;

				/* Child */
				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				stress_parent_died_alarm();
				(void)sched_settings_apply(true);

				/* Make sure this is killable by OOM killer */
				stress_set_oom_adjustment(args, true);

				if (do_flag_perm)
					UNSHARE(clone_flag, duration, count, &rc);
#if defined(CLONE_FS)
				UNSHARE(CLONE_FS, duration, count, &rc);
#endif
#if defined(CLONE_FILES)
				UNSHARE(CLONE_FILES, duration, count, &rc);
#endif
#if defined(CLONE_NEWCGROUP)
				UNSHARE(CLONE_NEWCGROUP, duration, count, &rc);
#endif
#if defined(CLONE_NEWIPC)
				UNSHARE(CLONE_NEWIPC, duration, count, &rc);
#endif
#if defined(CLONE_NEWNET)
				/*
				 *  CLONE_NEWNET when running as root on
				 *  hundreds of processes can be stupidly
				 *  expensive on older kernels so limit
				 *  this to just one per stressor instance
				 *  and don't unshare of root
				 */
				if ((n == 0) && (euid != 0))
					UNSHARE(CLONE_NEWNET, duration, count, &rc);
#endif
#if defined(CLONE_NEWNS)
				UNSHARE(CLONE_NEWNS, duration, count, &rc);
#endif
#if defined(CLONE_NEWPID)
				UNSHARE(CLONE_NEWPID, duration, count, &rc);
#endif
#if defined(CLONE_NEWUSER)
				UNSHARE(CLONE_NEWUSER, duration, count, &rc);
#endif
#if defined(CLONE_NEWUTS)
				UNSHARE(CLONE_NEWUTS, duration, count, &rc);
#endif
#if defined(CLONE_SYSVSEM)
				UNSHARE(CLONE_SYSVSEM, duration, count, &rc);
#endif
#if defined(CLONE_THREAD)
				UNSHARE(CLONE_THREAD, duration, count, &rc);
#endif
#if defined(CLONE_SIGHAND)
				UNSHARE(CLONE_SIGHAND, duration, count, &rc);
#endif
#if defined(CLONE_VM)
				UNSHARE(CLONE_VM, duration, count, &rc);
#endif
				_exit(0);
			}
		}
		for (i = 0; i < n; i++) {
			if (unshare_info[i].pid > 1)
				stress_kill_and_wait(args, unshare_info[i].pid, SIGALRM, false);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	for (i = 0; i < MAX_PIDS; i++) {
		total_duration += unshare_info[i].duration;
		total_count += unshare_info[i].count;
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (total_count > 0.0) ? total_duration / total_count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per unshare call",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	if (clone_flag_perms)
		free(clone_flag_perms);

	(void)munmap((void *)unshare_info, unshare_info_size);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_unshare_info = {
	.stressor = stress_unshare,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_unshare_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without unshare() system call"
};
#endif
