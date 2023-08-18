// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-pragma.h"

#define RMAP_CHILD_MAX		(16)
#define MAPPINGS_MAX		(64)
#define MAPPING_PAGES		(16)

static const stress_help_t help[] = {
	{ NULL,	"rmap N",	"start N workers that stress reverse mappings" },
	{ NULL,	"rmap-ops N",	"stop after N rmap bogo operations" },
	{ NULL,	NULL,		NULL }
};

static void *counter_lock;

/*
 *  [ MAPPING 0 ]
 *  [ page ][ MAPPING 1 ]
 *  [ page ][page ][ MAPPING 2]
 *  [ page ][page ][ page ][ MAPPING 3]
 *
 *  file size = ((MAPPINGS_MAX - 1) + MAPPING_PAGES) * page_size;
 */

/*
 *  stress_rmap_handler()
 *      rmap signal handler
 */
static void MLOCKED_TEXT NORETURN stress_rmap_handler(int signum)
{
	(void)signum;

	_exit(0);
}

static int OPTIMIZE3 stress_rmap_touch(
	const stress_args_t *args,
	const int child_index,
	uint32_t *addr,
	const size_t sz)
{
	register uintptr_t *begin = ((uintptr_t *)addr) + child_index;
	register const uintptr_t *end = (uintptr_t *)((uintptr_t)addr + sz);
	register uintptr_t *ptr;
	register const size_t inc = RMAP_CHILD_MAX * sizeof(uintptr_t);
	register uintptr_t mix = stress_mwc64();

	/* fill and put check value in that always has lowest bit set */
PRAGMA_UNROLL_N(8)
	for (ptr = begin; ptr < end; ptr += inc) {
		register uintptr_t val = (uintptr_t)ptr ^ mix;

		*ptr = val;
	}

	/* read back and check */
PRAGMA_UNROLL_N(8)
	for (ptr = begin; ptr < end; ptr += inc) {
		register uintptr_t chk = (uintptr_t)ptr ^ mix;

		if (*ptr != chk) {
			pr_fail("%s: address 0x%p check failure, "
				"got 0x%" PRIxPTR ", "
				"expected 0x%" PRIxPTR "\n",
				args->name, ptr, chk, *ptr);
			return -1;
		}
	}
	return 0;
}

static void NORETURN stress_rmap_child(
	const stress_args_t *args,
	const size_t page_size,
	const int child_index,
	uint32_t *mappings[MAPPINGS_MAX])
{
	const size_t sz = MAPPING_PAGES * page_size;
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t i;
		const uint8_t rnd8 = stress_mwc8();
		const int sync_flag = (rnd8 & 0x80) ? MS_ASYNC : MS_SYNC;

		switch (rnd8 & 3) {
		case 0: for (i = 0; i < MAPPINGS_MAX; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (!stress_bogo_inc_lock(args, counter_lock, false))
						break;
					if (stress_rmap_touch(args, child_index, mappings[i], sz) < 0) {
						rc = EXIT_FAILURE;
						goto fail;
					}
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		case 1: for (i = MAPPINGS_MAX - 1; i >= 0; i--) {
				if (mappings[i] != MAP_FAILED) {
					if (!stress_bogo_inc_lock(args, counter_lock, false))
						break;
					if (stress_rmap_touch(args, child_index, mappings[i], sz) < 0) {
						rc = EXIT_FAILURE;
						goto fail;
					}
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		case 2: for (i = 0; i < MAPPINGS_MAX; i++) {
				const size_t j = stress_mwc32modn(MAPPINGS_MAX);

				if (mappings[j] != MAP_FAILED) {
					if (!stress_bogo_inc_lock(args, counter_lock, false))
						break;
					if (stress_rmap_touch(args, child_index, mappings[j], sz) < 0) {
						rc = EXIT_FAILURE;
						goto fail;
					}
					(void)shim_msync(mappings[j], sz, sync_flag);
				}
			}
			break;
		case 3: for (i = 0; i < MAPPINGS_MAX - 1; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (!stress_bogo_inc_lock(args, counter_lock, false))
						break;
					if (stress_rmap_touch(args, child_index, mappings[i], sz) > 0) {
						rc = EXIT_FAILURE;
						goto fail;
					}
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		}
	} while (stress_bogo_inc_lock(args, counter_lock, true));

fail:
	stress_set_proc_state(args->name, STRESS_STATE_WAIT);
	_exit(rc);
}

/*
 *  stress_rmap()
 *	stress mmap
 */
static int stress_rmap(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t sz = ((MAPPINGS_MAX - 1) + MAPPING_PAGES) * page_size;
	int fd = -1, rc;
	size_t i;
	pid_t pids[RMAP_CHILD_MAX];
	uint32_t *mappings[MAPPINGS_MAX];
	uint32_t *paddings[MAPPINGS_MAX];
	char filename[PATH_MAX];

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)shim_memset(pids, 0, sizeof(pids));

	for (i = 0; i < MAPPINGS_MAX; i++) {
		mappings[i] = MAP_FAILED;
		paddings[i] = MAP_FAILED;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return stress_exit_status((int)-rc);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_err("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		(void)stress_lock_destroy(counter_lock);

		return (int)rc;
	}
	(void)shim_unlink(filename);

	if (shim_fallocate(fd, 0, 0, (off_t)sz) < 0) {
		pr_err("%s: fallocate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		(void)stress_lock_destroy(counter_lock);

		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < MAPPINGS_MAX; i++) {
		const off_t offset = (off_t)(i * page_size);

		if (!stress_continue(args))
			goto cleanup;

		mappings[i] =
			(uint32_t *)mmap(0, MAPPING_PAGES * page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, offset);
		/* Squeeze at least a page in between each mapping */
		paddings[i] =
			(uint32_t *)mmap(0, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	}

	/*
	 *  Spawn children workers
	 */
	for (i = 0; i < RMAP_CHILD_MAX; i++) {
		if (!stress_continue(args))
			goto cleanup;

		pids[i] = fork();
		if (pids[i] < 0) {
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			goto cleanup;
		} else if (pids[i] == 0) {
			if (stress_sighandler(args->name, SIGALRM,
			    stress_rmap_handler, NULL) < 0)
				_exit(EXIT_FAILURE);

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);

			stress_rmap_child(args, page_size, i, mappings);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Wait for SIGINT or SIGALRM
	 */
	while (stress_bogo_inc_lock(args, counter_lock, false)) {
		pause();
	}

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rc = stress_kill_and_wait_many(args, pids, RMAP_CHILD_MAX, SIGALRM, true);

	for (i = 0; i < MAPPINGS_MAX; i++) {
		if (mappings[i] != MAP_FAILED)
			(void)munmap((void *)mappings[i], MAPPING_PAGES * page_size);
		if (paddings[i] != MAP_FAILED)
			(void)munmap((void *)paddings[i], page_size);
	}

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_lock_destroy(counter_lock);

	return rc;
}

stressor_info_t stress_rmap_info = {
	.stressor = stress_rmap,
	.class = CLASS_OS | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help
};
