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
#include "core-killpid.h"
#include "core-out-of-memory.h"
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
	stress_args_t *args,
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
		register const uintptr_t val = (uintptr_t)ptr ^ mix;

		*ptr = val;
	}

	/* read back and check */
PRAGMA_UNROLL_N(8)
	for (ptr = begin; ptr < end; ptr += inc) {
		register const uintptr_t chk = (uintptr_t)ptr ^ mix;

		if (UNLIKELY(*ptr != chk)) {
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
	stress_args_t *args,
	const size_t page_size,
	const int child_index,
	uint32_t *mappings[MAPPINGS_MAX])
{
	const size_t sz = MAPPING_PAGES * page_size;
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register ssize_t i;
		const uint8_t rnd8 = stress_mwc8();
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
		const int sync_flag = (rnd8 & 0x80) ? MS_ASYNC : MS_SYNC;
#endif

		switch (rnd8 & 3) {
		case 0:
			for (i = 0; i < MAPPINGS_MAX; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
						break;
					if (UNLIKELY(stress_rmap_touch(args, child_index, mappings[i], sz) < 0)) {
						rc = EXIT_FAILURE;
						goto fail;
					}
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
					(void)shim_msync(mappings[i], sz, sync_flag);
#endif
				}
			}
			break;
		case 1:
			for (i = MAPPINGS_MAX - 1; i >= 0; i--) {
				if (mappings[i] != MAP_FAILED) {
					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
						break;
					if (UNLIKELY(stress_rmap_touch(args, child_index, mappings[i], sz) < 0)) {
						rc = EXIT_FAILURE;
						goto fail;
					}
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
					(void)shim_msync(mappings[i], sz, sync_flag);
#endif
				}
			}
			break;
		case 2:
			for (i = 0; i < MAPPINGS_MAX; i++) {
				const size_t j = stress_mwc32modn(MAPPINGS_MAX);

				if (mappings[j] != MAP_FAILED) {
					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
						break;
					if (UNLIKELY(stress_rmap_touch(args, child_index, mappings[j], sz) < 0)) {
						rc = EXIT_FAILURE;
						goto fail;
					}
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
					(void)shim_msync(mappings[j], sz, sync_flag);
#endif
				}
			}
			break;
		case 3:
			for (i = 0; i < MAPPINGS_MAX - 1; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, false)))
						break;
					if (UNLIKELY(stress_rmap_touch(args, child_index, mappings[i], sz) > 0)) {
						rc = EXIT_FAILURE;
						goto fail;
					}
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC)
					(void)shim_msync(mappings[i], sz, sync_flag);
#endif
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
static int stress_rmap(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t sz = ((MAPPINGS_MAX - 1) + MAPPING_PAGES) * page_size;
	int fd = -1, rc;
	size_t i;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	uint32_t *mappings[MAPPINGS_MAX];
	uint32_t *paddings[MAPPINGS_MAX];
	char filename[PATH_MAX];

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_sync_s_pids_mmap(RMAP_CHILD_MAX);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, RMAP_CHILD_MAX, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		(void)stress_sync_s_pids_munmap(s_pids, RMAP_CHILD_MAX);
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < MAPPINGS_MAX; i++) {
		mappings[i] = MAP_FAILED;
		paddings[i] = MAP_FAILED;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		(void)stress_lock_destroy(counter_lock);
		(void)stress_sync_s_pids_munmap(s_pids, RMAP_CHILD_MAX);
		return stress_exit_status((int)-rc);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_err("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		(void)stress_lock_destroy(counter_lock);
		(void)stress_sync_s_pids_munmap(s_pids, RMAP_CHILD_MAX);

		return (int)rc;
	}
	(void)shim_unlink(filename);

	if (shim_fallocate(fd, 0, 0, (off_t)sz) < 0) {
		pr_err("%s: fallocate failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		(void)stress_lock_destroy(counter_lock);
		(void)stress_sync_s_pids_munmap(s_pids, RMAP_CHILD_MAX);

		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < MAPPINGS_MAX; i++) {
		const off_t offset = (off_t)(i * page_size);

		if (UNLIKELY(!stress_continue(args)))
			goto cleanup;

		mappings[i] =
			(uint32_t *)mmap(NULL, MAPPING_PAGES * page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, offset);
		/* Squeeze at least a page in between each mapping */
		paddings[i] =
			(uint32_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (paddings[i] != MAP_FAILED)
			stress_set_vma_anon_name(paddings[i], page_size, "mmap-padding");
	}

	/*
	 *  Spawn children workers
	 */
	for (i = 0; i < RMAP_CHILD_MAX; i++) {
		stress_sync_start_init(&s_pids[i]);

		if (UNLIKELY(!stress_continue(args)))
			goto cleanup;

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			goto cleanup;
		} else if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			if (stress_sighandler(args->name, SIGALRM,
			    stress_rmap_handler, NULL) < 0)
				_exit(EXIT_FAILURE);

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);

			stress_rmap_child(args, page_size, i, mappings);
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Wait for SIGINT or SIGALRM
	 */
	while (stress_bogo_inc_lock(args, counter_lock, false)) {
		(void)shim_pause();
	}

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rc = stress_kill_and_wait_many(args, s_pids, RMAP_CHILD_MAX, SIGALRM, true);

	for (i = 0; i < MAPPINGS_MAX; i++) {
		if (mappings[i] != MAP_FAILED)
			(void)munmap((void *)mappings[i], MAPPING_PAGES * page_size);
		if (paddings[i] != MAP_FAILED)
			(void)munmap((void *)paddings[i], page_size);
	}

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_lock_destroy(counter_lock);
	(void)stress_sync_s_pids_munmap(s_pids, RMAP_CHILD_MAX);

	return rc;
}

const stressor_info_t stress_rmap_info = {
	.stressor = stress_rmap,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help
};
