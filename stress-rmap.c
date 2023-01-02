/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

	(void)kill(getppid(), SIGALRM);
	_exit(0);
}

static void stress_rmap_touch(uint32_t *ptr, const size_t sz)
{
	const uint32_t *end = (uint32_t *)((uintptr_t)ptr + sz);
	static uint32_t val = 0;
	register const size_t inc = 64 >> 2; /* sizeof(*ptr) */

	while (ptr < end) {
		/* Bump val, never fill memory with non-zero val */
		val++;
		val = val ? val : 1;

		*ptr = val;
		ptr += inc;
	}
}

static void NORETURN stress_rmap_child(
	const stress_args_t *args,
	const size_t page_size,
	uint32_t *mappings[MAPPINGS_MAX])
{
	const size_t sz = MAPPING_PAGES * page_size;

	do {
		ssize_t i;
		const uint8_t rnd8 = stress_mwc8();
		const int sync_flag = (rnd8 & 0x80) ? MS_ASYNC : MS_SYNC;

		switch (rnd8 & 3) {
		case 0: for (i = 0; i < MAPPINGS_MAX; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (!inc_counter_lock(args, counter_lock, false))
						break;
					stress_rmap_touch(mappings[i], sz);
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		case 1: for (i = MAPPINGS_MAX - 1; i >= 0; i--) {
				if (mappings[i] != MAP_FAILED) {
					if (!inc_counter_lock(args, counter_lock, false))
						break;
					stress_rmap_touch(mappings[i], sz);
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		case 2: for (i = 0; i < MAPPINGS_MAX; i++) {
				size_t j = stress_mwc32() % MAPPINGS_MAX;

				if (mappings[j] != MAP_FAILED) {
					if (!inc_counter_lock(args, counter_lock, false))
						break;
					stress_rmap_touch(mappings[j], sz);
					(void)shim_msync(mappings[j], sz, sync_flag);
				}
			}
			break;
		case 3: for (i = 0; i < MAPPINGS_MAX - 1; i++) {
				if (mappings[i] != MAP_FAILED) {
					if (!inc_counter_lock(args, counter_lock, false))
						break;
					stress_rmap_touch(mappings[i], sz);
					(void)shim_msync(mappings[i], sz, sync_flag);
				}
			}
			break;
		}
	} while (inc_counter_lock(args, counter_lock, true));

	(void)kill(getppid(), SIGALRM);

	_exit(0);
}

/*
 *  stress_rmap()
 *	stress mmap
 */
static int stress_rmap(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t sz = ((MAPPINGS_MAX - 1) + MAPPING_PAGES) * page_size;
	int fd = -1;
	size_t i;
	ssize_t rc;
	pid_t pids[RMAP_CHILD_MAX];
	uint32_t *mappings[MAPPINGS_MAX];
	uint32_t *paddings[MAPPINGS_MAX];
	char filename[PATH_MAX];

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(pids, 0, sizeof(pids));

	for (i = 0; i < MAPPINGS_MAX; i++) {
		mappings[i] = MAP_FAILED;
		paddings[i] = MAP_FAILED;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args->name, true);

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

		if (!keep_stressing(args))
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
		if (!keep_stressing(args))
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
			stress_set_oom_adjustment(args->name, true);
			stress_rmap_child(args, page_size, mappings);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Wait for SIGINT or SIGALRM
	 */
	while (inc_counter_lock(args, counter_lock, false)) {
		pause();
	}

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  Kill and wait for children
	 */
	for (i = 0; i < RMAP_CHILD_MAX; i++) {
		if (pids[i] <= 0)
			continue;
		(void)stress_killpid(pids[i]);
	}
	for (i = 0; i < RMAP_CHILD_MAX; i++) {
		int status, ret;

		if (pids[i] <= 0)
			continue;

		ret = shim_waitpid(pids[i], &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pids[i], SIGTERM);
			(void)stress_killpid(pids[i]);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	for (i = 0; i < MAPPINGS_MAX; i++) {
		if (mappings[i] != MAP_FAILED)
			(void)munmap((void *)mappings[i], MAPPING_PAGES * page_size);
		if (paddings[i] != MAP_FAILED)
			(void)munmap((void *)paddings[i], page_size);
	}

	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_lock_destroy(counter_lock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_rmap_info = {
	.stressor = stress_rmap,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help
};
