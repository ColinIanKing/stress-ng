// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#define UNSET_MLOCK_PROCS		(0)
#define DEFAULT_MLOCK_PROCS		(1024)

static const stress_help_t help[] = {
	{ NULL,	"mlockmany N",	   	"start N workers exercising many mlock/munlock processes" },
	{ NULL,	"mlockmany-ops N", 	"stop after N mlockmany bogo operations" },
	{ NULL, "mlockmany-procs N",	"use N child processes to mlock regions" },
	{ NULL,	NULL,		   	NULL }
};

/*
 *  stress_set_mlockmany_procs()
 *      set number of processes to spawn to mlock pages
 */
static int stress_set_mlockmany_procs(const char *opt)
{
	size_t mlockmany_procs;

	mlockmany_procs = (size_t)stress_get_uint64(opt);
	stress_check_range("mlockmany-procs", mlockmany_procs,
		1, 1000000);
	return stress_set_setting("mlockmany-procs", TYPE_ID_SIZE_T, &mlockmany_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mlockmany_procs,	stress_set_mlockmany_procs },
	{ 0,			NULL }
};

#if defined(HAVE_MLOCK)

static int stress_mlock_interruptible(
	const stress_args_t *args,
	void *addr,
	size_t len)
{
	const size_t chunk_size = args->page_size << 4;
	uintptr_t ptr = (uintptr_t)addr;

	while ((len > 0) && (stress_continue(args))) {
		size_t sz = (len > chunk_size) ? chunk_size : len;
		int ret;

		ret = shim_mlock((void *)ptr, sz);
		if (ret < 0)
			return ret;
		ptr += sz;
		len -= sz;
	}
	return 0;
}

static int stress_munlock_interruptible(
	const stress_args_t *args,
	void *addr,
	size_t len)
{
	const size_t chunk_size = args->page_size << 4;
	uintptr_t ptr = (uintptr_t)addr;

	while ((len > 0) && (stress_continue(args))) {
		size_t sz = (len > chunk_size) ? chunk_size : len;
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
static int stress_mlockmany(const stress_args_t *args)
{
	pid_t *pids;
	int ret;
#if defined(RLIMIT_MEMLOCK)
	struct rlimit rlim;
#endif
	size_t mlock_size, mlockmany_procs = UNSET_MLOCK_PROCS;

	(void)stress_get_setting("mlockmany-procs", &mlockmany_procs);

	stress_set_oom_adjustment(args, true);

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	if (mlockmany_procs == UNSET_MLOCK_PROCS) {
		mlockmany_procs = args->num_instances > 0 ? DEFAULT_MLOCK_PROCS / args->num_instances : 1;
		if (mlockmany_procs < 1)
			mlockmany_procs = 1;
	}

	pids = calloc((size_t)mlockmany_procs, sizeof(*pids));
	if (!pids) {
		pr_inf_skip("%s: cannot allocate pids array, skipping stressor\n", args->name);
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
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int n;
		size_t shmall, freemem, totalmem, freeswap, totalswap, last_freeswap, last_totalswap;

		(void)shim_memset(pids, 0, sizeof(*pids) * mlockmany_procs);
		stress_get_memlimits(&shmall, &freemem, &totalmem, &last_freeswap, &last_totalswap);

		for (n = 0; stress_continue(args) && (n < mlockmany_procs); n++) {
			pid_t pid;

			/* In case we've missed SIGALRM */
			if (stress_time_now() > args->time_end) {
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

				/* In case we've missed SIGALRM */
				if (stress_time_now() > args->time_end)
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
					if (!stress_continue(args))
						_exit(0);
					ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
					if (ptr != MAP_FAILED)
						break;
					mmap_size >>= 1;
				}
				if (ptr == MAP_FAILED)
					_exit(0);

				(void)stress_mincore_touch_pages(ptr, mmap_size);

				mlock_size = mmap_size;
				while (mlock_size > args->page_size) {
					if (!stress_continue(args))
						_exit(0);
					ret = stress_mlock_interruptible(args, ptr, mlock_size);
					if (ret == 0)
						break;
					mlock_size >>= 1;
				}

				while (stress_continue(args)) {
					(void)stress_munlock_interruptible(args, ptr, mlock_size);
					if (!stress_continue(args))
						goto unmap;
					(void)stress_mlock_interruptible(args, ptr, mlock_size);
					if (!stress_continue(args))
						goto unlock;
					/* Try invalid sizes */
					(void)shim_mlock(ptr, 0);
					(void)shim_munlock(ptr, 0);

					(void)stress_mlock_interruptible(args, ptr, mlock_size << 1);
					if (!stress_continue(args))
						goto unlock;
					(void)stress_munlock_interruptible(args, ptr, mlock_size << 1);
					if (!stress_continue(args))
						goto unlock;

					(void)shim_munlock(ptr, ~(size_t)0);
					if (!stress_continue(args))
						goto unlock;
					(void)shim_usleep_interruptible(10000);
				}
unlock:
				(void)stress_munlock_interruptible(args, ptr, mlock_size);
unmap:
				(void)munmap(ptr, mmap_size);
				_exit(0);
			}
			pids[n] = pid;
			if (pid > 1) {
				stress_bogo_inc(args);
			} else if (pid < 0)
				break;
			if (!stress_continue(args))
				break;
		}
		stress_kill_and_wait_many(args, pids, n, SIGALRM, false);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(pids);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mlockmany_info = {
	.stressor = stress_mlockmany,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};

#else

stressor_info_t stress_mlockmany_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without mlock() support"
};

#endif

