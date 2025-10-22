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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-out-of-memory.h"

#define UNSET_MLOCKMANY_PROCS		(0)
#define DEFAULT_MLOCKMANY_PROCS		(1024)

#define MIN_MLOCKMANY_PROCS		(1)
#define MAX_MLOCKMANY_PROCS		(1000000)

static const stress_help_t help[] = {
	{ NULL,	"mlockmany N",	   	"start N workers exercising many mlock/munlock processes" },
	{ NULL,	"mlockmany-ops N", 	"stop after N mlockmany bogo operations" },
	{ NULL, "mlockmany-procs N",	"use N child processes to mlock regions" },
	{ NULL,	NULL,		   	NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mlockmany_procs, "mlockmany-procs", TYPE_ID_SIZE_T, MIN_MLOCKMANY_PROCS, MAX_MLOCKMANY_PROCS, NULL },
	END_OPT,
};

#if defined(HAVE_MLOCK)

static int stress_mlock_interruptible(
	stress_args_t *args,
	void *addr,
	size_t len)
{
	const size_t chunk_size = args->page_size << 4;
	uintptr_t ptr = (uintptr_t)addr;

	while ((len > 0) && (stress_continue(args))) {
		const size_t sz = (len > chunk_size) ? chunk_size : len;
		int ret;

		if (stress_low_memory(sz))
			break;
		ret = shim_mlock((void *)ptr, sz);
		if (ret < 0)
			return ret;
		ptr += sz;
		len -= sz;
	}
	return 0;
}

static int stress_munlock_interruptible(
	stress_args_t *args,
	void *addr,
	size_t len)
{
	const size_t chunk_size = args->page_size << 4;
	uintptr_t ptr = (uintptr_t)addr;

	while ((len > 0) && (stress_continue(args))) {
		const size_t sz = (len > chunk_size) ? chunk_size : len;
		int ret;

		ret = shim_munlock((void *)ptr, sz);
		if (ret < 0)
			return ret;
		ptr += sz;
		len -= sz;
	}
	return 0;
}

/*
 *  stress_mlockmany()
 *	stress by forking and exiting
 */
static int stress_mlockmany_child(stress_args_t *args, void *context)
{
	stress_pid_t *s_pids;
	int ret;
#if defined(RLIMIT_MEMLOCK)
	struct rlimit rlim;
#endif
	size_t mlock_size, mlockmany_procs = UNSET_MLOCKMANY_PROCS;

	(void)context;

	if (!stress_get_setting("mlockmany-procs", &mlockmany_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mlockmany_procs = MAX_MLOCKMANY_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mlockmany_procs = MIN_MLOCKMANY_PROCS;
	}

	stress_set_oom_adjustment(args, true);

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	if (mlockmany_procs == UNSET_MLOCKMANY_PROCS) {
		mlockmany_procs = args->instances > 0 ? DEFAULT_MLOCKMANY_PROCS / args->instances : 1;
		if (mlockmany_procs < 1)
			mlockmany_procs = 1;
	}

	s_pids = stress_sync_s_pids_mmap(mlockmany_procs);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
			args->name, mlockmany_procs, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

#if defined(RLIMIT_MEMLOCK)
	ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
	if (ret < 0) {
		mlock_size = 8 * MB;
	} else {
		mlock_size = rlim.rlim_cur;
	}
#else
	mlock_size = args->page_size * 1024;
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int n;
		size_t shmall, freemem, totalmem, freeswap, totalswap, last_freeswap, last_totalswap;

		(void)shim_memset(s_pids, 0, sizeof(*s_pids) * mlockmany_procs);
		stress_get_memlimits(&shmall, &freemem, &totalmem, &last_freeswap, &last_totalswap);

		for (n = 0; LIKELY(stress_continue(args) && (n < mlockmany_procs)); n++) {
			pid_t pid;

			s_pids[n].pid = -1;

			/* In case we've missed SIGALRM */
			if (UNLIKELY(stress_time_now() > args->time_end)) {
				stress_continue_set_flag(false);
				break;
			}

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

			/* We detected swap being used, bail out */
			if (last_freeswap > freeswap)
				break;

			/* Keep track of expanding free swap space */
			if (freeswap > last_freeswap)
				last_freeswap = freeswap;

			pid = fork();
			if (pid == 0) {
				void *ptr = MAP_FAILED;
				size_t mmap_size = mlock_size;

				stress_set_proc_state(args->name, STRESS_STATE_RUN);

				/* In case we've missed SIGALRM */
				if (UNLIKELY(stress_time_now() > args->time_end))
					_exit(0);

				stress_parent_died_alarm();
				stress_set_oom_adjustment(args, true);
				(void)sched_settings_apply(true);

				shim_mlockall(0);
				stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
				/* We detected swap being used, bail out */
				if (last_freeswap > freeswap)
					_exit(0);

				while (mmap_size > args->page_size) {
					if (UNLIKELY(!stress_continue(args)))
						_exit(0);
					ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
					if (ptr != MAP_FAILED)
						break;
					mmap_size >>= 1;
				}
				if (ptr == MAP_FAILED)
					_exit(0);

				stress_set_vma_anon_name(ptr, mmap_size, "mlocked-pages");
				(void)stress_mincore_touch_pages(ptr, mmap_size);
				(void)stress_madvise_mergeable(ptr, mmap_size);

				mlock_size = mmap_size;
				while (mlock_size > args->page_size) {
					if (UNLIKELY(!stress_continue(args)))
						_exit(0);
					ret = stress_mlock_interruptible(args, ptr, mlock_size);
					if (ret == 0)
						break;
					mlock_size >>= 1;
				}

				while (stress_continue(args)) {
					(void)stress_munlock_interruptible(args, ptr, mlock_size);
					if (UNLIKELY(!stress_continue(args)))
						goto unmap;
					(void)stress_mlock_interruptible(args, ptr, mlock_size);
					if (UNLIKELY(!stress_continue(args)))
						goto unlock;
					/* Try invalid sizes */
					(void)shim_mlock(ptr, 0);
					(void)shim_munlock(ptr, 0);

					(void)stress_mlock_interruptible(args, ptr, mlock_size << 1);
					if (UNLIKELY(!stress_continue(args)))
						goto unlock;
					(void)stress_munlock_interruptible(args, ptr, mlock_size << 1);
					if (UNLIKELY(!stress_continue(args)))
						goto unlock;

					(void)shim_munlock(ptr, ~(size_t)0);
					if (UNLIKELY(!stress_continue(args)))
						goto unlock;
					(void)shim_usleep_interruptible(10000);
				}
unlock:
				(void)stress_munlock_interruptible(args, ptr, mlock_size);
unmap:
				(void)munmap(ptr, mmap_size);
				_exit(0);
			}
			s_pids[n].pid = pid;
			if (pid > 1) {
				stress_bogo_inc(args);
			} else if (pid < 0)
				break;
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
		stress_kill_and_wait_many(args, s_pids, n, SIGALRM, false);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_sync_s_pids_munmap(s_pids, mlockmany_procs);

	return EXIT_SUCCESS;
}

/*
 *  stress_mlockmany()
 *	stress by forking and exiting
 */
static int stress_mlockmany(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mlockmany_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_mlockmany_info = {
	.stressor = stress_mlockmany,
	.classifier = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_mlockmany_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without mlock() support"
};

#endif

