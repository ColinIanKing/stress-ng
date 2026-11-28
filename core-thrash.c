/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-thrash.h"

#include <ctype.h>
#include <sched.h>

#if defined(__linux__) &&	\
    defined(HAVE_PTRACE)

#define KSM_RUN_MERGE		"1"

static pid_t thrash_pid;
static pid_t parent_pid;
static volatile bool thrash_run;
static sigjmp_buf jmp_env;
static bool jmp_env_set;

/*
 *  stress_thrash_handler()
 *	handle signals, set flag to stop thrash child process
 */
static void MLOCKED_TEXT stress_thrash_handler(int signum)
{
	(void)signum;

	thrash_run = false;
}

/*
 *  stress_thrash_pagein_handler()
 *	jmp back to stress_pagein_self on signal
 */
static void MLOCKED_TEXT stress_thrash_pagein_handler(int signum)
{
	(void)signum;

	if (jmp_env_set) {
		siglongjmp(jmp_env, 1);
		stress_no_return();
	}
}

/*
 *  stress_thrash_state()
 *	set name of thrasher child process
 */
static void stress_thrash_state(const char *state)
{
	stress_proc_name_state_str_set("thrash", state);
}

/*
 *  stress_thrash_pagein_self()
 *	force pages into memory for current process
 */
static void stress_thrash_pagein_self(const stress_mmaps_t *mmaps, const size_t page_size)
{
	int ret;
	struct sigaction bus_action;
	struct sigaction segv_action;
	size_t i;
	static const char name[] = "core-thrash";

	jmp_env_set = false;

	VOID_RET(int, stress_signal_handler(name, SIGBUS, stress_thrash_pagein_handler, &bus_action));
	VOID_RET(int, stress_signal_handler(name, SIGSEGV, stress_thrash_pagein_handler, &segv_action));

	ret = sigsetjmp(jmp_env, 1);
	if (ret == 1)
		goto err;

	stress_thrash_state("pagein");

	for (i = 0; thrash_run && (i < mmaps->n_mmaps); i++) {
		const stress_mmap_t *mmap = &mmaps->mmaps[i];
		bool writeable;
		uintptr_t off;

		/* ignore non-readable mappings */
		if (!(mmap->prot & STRESS_MMAP_READ))
			continue;
		/* ignore non-writeable mappings */
		writeable = !!(mmaps->mmaps[i].prot & STRESS_MMAP_WRITE);

		for (off = mmap->mmap_begin; thrash_run && (off < mmap->mmap_end); off += page_size) {
			volatile uint8_t *ptr = (volatile uint8_t *)(uintptr_t)off;
			const uint8_t value = *ptr;

			if (writeable)
				*ptr = value;
		}
	}
err:
	jmp_env_set = false;
	/* Restore action */
	(void)sigaction(SIGBUS, &bus_action, NULL);
	(void)sigaction(SIGSEGV, &segv_action, NULL);
}

/*
 *  stress_pagein_proc()
 *	force pages into memory for a given process
 */
static void stress_pagein_proc(const pid_t pid, const size_t page_size)
{
	char path[PATH_MAX];
	int fdmem;
	size_t i;
	stress_mmaps_t mmaps;
	const size_t mask = STRESS_MMAP_READ | STRESS_MMAP_PRIVATE;

	if ((pid == parent_pid) || (pid == getpid()))
		return;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/mem", (intmax_t)pid);
	fdmem = open(path, O_RDONLY);
	if (fdmem < 0)
		return;

	if (stress_mmaps_get(pid, page_size, mask, false, true, &mmaps) < 0) {
		(void)close(fdmem);
		return;
	}

	for (i = 0; thrash_run && (i < mmaps.n_mmaps); i++) {
		uintptr_t off;
		uintptr_t end;
		const uintptr_t max_off = ((uintptr_t)~(off_t)0) - (page_size - 1);
		const stress_mmap_t *mmap = &mmaps.mmaps[i];

		if ((mmap->prot & mask) != mask)
			continue;

		end = mmap->mmap_end;
		if (end >= max_off)
			end = max_off;

		for (off = mmap->mmap_begin; thrash_run && (off < end); off += page_size) {
			unsigned long int data;
			off_t ret;
			off_t pos = (off_t)off;

			ret = lseek(fdmem, pos, SEEK_SET);
			if (ret != pos)
				continue;
			VOID_RET(ssize_t, read(fdmem, &data, sizeof(data)));
		}
	}

	stress_mmaps_free(&mmaps);

	(void)close(fdmem);

	return;
}

#if defined(__linux__)
/*
 *  stress_proc_file_read()
 *	read a proc file
 */
static void stress_file_read(const char *filename)
{
	char buf[4096];
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return;

	while (read(fd, buf, sizeof(buf)) > 0)
		;
	(void)close(fd);
}
#endif

/*
 *  stress_thrash_sys_memory()
 *	stress sys files that are memory related
 */
static inline void stress_thrash_sys_memory(void)
{
#if defined(__linux__)
	static const char * const sys_files[] = {
		"/sys/kernel/vmcoreinfo",
		"/sys/kernel/debug/percpu_stats",
	};

	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(sys_files); i++) {
		stress_file_read(sys_files[i]);
	}
#endif
}

/*
 *  stress_thrash_proc_memory()
 *	stress proc files that are memory related
 */
static inline void stress_thrash_proc_memory(void)
{
#if defined(__linux__)
	static const char * const proc_files[] = {
		"/proc/allocinfo",
		"/proc/buddyinfo",
		"/proc/meminfo",
		"/proc/slabinfo",
		"/proc/pagetypeinfo",
		"/proc/pressure/memory",
		"/proc/vmallocinfo",
		"/proc/zoneinfo",
	};

	size_t i;
	DIR *dir;
	const struct dirent *d;

	for (i = 0; i < SIZEOF_ARRAY(proc_files); i++) {
		stress_file_read(proc_files[i]);
	}

	/*
	 *  scan through all /proc/$PID directories
	 *  reading proc_pid_files
	 */
	dir = opendir("/proc");
	if (!dir)
		return;

	while ((d = readdir(dir)) != NULL) {
		static const char * const proc_pid_files[] = {
			"maps",
			"numa_maps",
			"projid_map",
			"smaps",
			"smaps_rollup",
		};

		if (!isdigit((unsigned char)d->d_name[0]))
			continue;

		for (i = 0; thrash_run && (i < SIZEOF_ARRAY(proc_pid_files)); i++) {
			char filename[PATH_MAX];

			(void)snprintf(filename, sizeof(filename),
				"/proc/%s/%s", d->d_name,
				proc_pid_files[i]);
			stress_file_read(filename);
		}
	}
	(void)closedir(dir);
#endif
}

/*
 *  stress_thrash_compact_memory()
 *	trigger memory compaction, Linux only
 */
static inline void stress_thrash_compact_memory(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("compact");
	VOID_RET(ssize_t, stress_fs_file_write("/proc/sys/vm/compact_memory", "1", 1));
#endif
}

/*
 *  stress_thrash_zone_reclaim()
 *	trigger reclaim when zones run out of memory
 */
static inline void stress_thrash_zone_reclaim(void)
{
#if defined(__linux__)
	char mode[2];

	if (!thrash_run)
		return;

	mode[0] = '0' + (stress_mwc8() & 7);
	mode[1] = '\0';

	stress_thrash_state("reclaim");
	VOID_RET(ssize_t, stress_fs_file_write("/proc/sys/vm/zone_reclaim_mode", mode, 1));
#endif
}

/*
 *  stress_thrash_kmemleak_scan()
 *	trigger kernel memory leak scan
 */
static inline void stress_thrash_kmemleak_scan(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("scan");
	VOID_RET(ssize_t, stress_fs_file_write("/sys/kernel/debug/kmemleak", "scan", 4));
#endif
}

/*
 *  stress_thrash_slab_shrink()
 *	shrink slabs to help release some memory
 */
static inline void stress_thrash_slab_shrink(void)
{
	DIR *dir;
	const struct dirent *d;
	static const char slabpath[] = "/sys/kernel/slab";

	/*
	 *  older shrink interface, may fail
	 */
	VOID_RET(ssize_t, stress_fs_file_write("/sys/kernel/slab/cache/shrink", "1", 1));

	dir = opendir(slabpath);
	if (!dir)
		return;

	stress_thrash_state("shrink");
	/*
	 *  shrink all slabs
	 */
	while ((d = readdir(dir)) != NULL) {
		if (isalpha((unsigned char)d->d_name[0]))  {
			char path[PATH_MAX];

			(void)snprintf(path, sizeof(path), "%s/%s", slabpath, d->d_name);
			VOID_RET(ssize_t, stress_fs_file_write(path, "1", 1));
		}
	}
	(void)closedir(dir);
}

/*
 *  stress_thrash_drop_caches()
 *	drop caches
 */
static inline void stress_thrash_drop_caches(void)
{
#if defined(__linux__)
	static int flags = STRESS_DROP_CACHE_PAGE_CACHE;

	stress_thrash_state("dropcache");
	stress_fs_drop_caches(flags);
	if (flags++ >= STRESS_DROP_CACHE_ALL)
		flags = STRESS_DROP_CACHE_PAGE_CACHE;
#endif
}

/*
 *  stress_thrash_merge_memory()
 *	trigger ksm memory merging, Linux only
 */
static inline void stress_thrash_merge_memory(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("merge");
	VOID_RET(ssize_t, stress_fs_file_write("/proc/sys/mm/ksm/run", KSM_RUN_MERGE, 1));
#endif
}

/*
 *  stress_thrash_pagein_all_procs()
 *	force pages into memory for all processes
 */
static void stress_thrash_pagein_all_procs(const size_t page_size)
{
	DIR *dp;
	const struct dirent *d;

	dp = opendir("/proc");
	if (!dp)
		return;

	while (thrash_run && ((d = readdir(dp)) != NULL)) {
		intmax_t pid;

		if (isdigit((unsigned char)d->d_name[0]) &&
		    sscanf(d->d_name, "%" SCNdMAX, &pid) == 1) {
			char procpath[128];
			struct stat statbuf;

			(void)snprintf(procpath, sizeof(procpath), "/proc/%" PRIdMAX, pid);
			if (shim_stat(procpath, &statbuf) < 0)
				continue;

			if (statbuf.st_uid == 0)
				continue;

			stress_pagein_proc((pid_t)pid, page_size);
		}
	}
	(void)closedir(dp);
}

#if defined(HAVE_LINUX_MEMPOLICY_H)
static void stress_thrash_move_pages(
	const stress_mmaps_t *mmaps,
	stress_numa_mask_t *numa_nodes)
{
	const size_t page_size = stress_memory_page_size_get();
	long int node = 0;
	size_t i;
	uint16_t j;

	/* start on a random node */
	for (j = 0; j < stress_mwc16modn((uint16_t)numa_nodes->nodes); j++)
		node = stress_numa_next_node(node, numa_nodes);

	stress_thrash_state("movepages");

	for (i = 0; thrash_run && (i < mmaps->n_mmaps); i++) {
		uintptr_t off;
		const stress_mmap_t *mmap = &mmaps->mmaps[i];

		for (off = mmap->mmap_begin; thrash_run && (off < mmap->mmap_end); off += page_size) {
			node = stress_numa_next_node(node, numa_nodes);
			if (LIKELY(node < INT_MAX)) {
				void *pages[1];
				int nodes[1];
				int states[1];
				const int flag = stress_mwc1() ? MPOL_MF_MOVE : MPOL_MF_MOVE_ALL;

				pages[0] = (void *)off;
				nodes[0] = (int)node;
				states[0] = 0;

				if (shim_move_pages(parent_pid, 1, pages, nodes, states, flag) < 0) {
					if (errno == ENOSYS)
						return;
				}
			}
		}
	}
}
#endif

static void stress_thrash_fragment_mappings(const stress_mmaps_t *mmaps)
{
	size_t i;

	stress_thrash_state("fragment");

	for (i = 0; thrash_run && (i < mmaps->n_mmaps); i++) {
		const stress_mmap_t *mmap = &mmaps->mmaps[i];

		/* ignore non-readable mappings */
		if (!(mmap->prot & STRESS_MMAP_READ))
			continue;
		/* ignore non-writeable mappings */
		if (!(mmap->prot & STRESS_MMAP_WRITE))
			continue;
		/* ignore private mappings */
		if (mmap->prot & STRESS_MMAP_PRIVATE)
			continue;

		stress_mmap_discontiguous((void *)mmap->mmap_begin, mmap->size);

	}
}

/*
 *  stress_thrash_start()
 *	start paging in thrash process
 */
int COLD stress_thrash_start(void)
{
#if defined(HAVE_LINUX_MEMPOLICY_H)
        stress_numa_mask_t *numa_mask;
        stress_numa_mask_t *numa_nodes;
	bool thrash_numa = true;
#endif
	if (geteuid() != 0) {
		pr_inf("not running as root, ignoring --thrash option\n");
		return -1;
	}
	if (thrash_pid) {
		pr_err("thrash background process already started\n");
		return -1;
	}

	parent_pid = getpid();
	thrash_run = true;
	thrash_pid = fork();
	if (thrash_pid < 0) {
		thrash_run = false;
		pr_err("thrash background process failed to fork, errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	} else if (thrash_pid == 0) {
		const size_t page_size = stress_memory_page_size_get();

#if defined(SCHED_RR)
		VOID_RET(int, stress_sched_set(getpid(), SCHED_RR, 10, true));
#endif
		stress_thrash_state("init");
		if (stress_signal_handler("main", SIGALRM, stress_thrash_handler, NULL) < 0)
			_exit(0);

#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(NULL, &numa_nodes, &numa_mask,
						"NUMA thrashing", &thrash_numa);
#endif
		while (thrash_run) {
			stress_mmaps_t mmaps;

			(void)stress_mmaps_get(-1, page_size, 0, false, true, &mmaps);

			if ((stress_mwc8() & 0x3) == 0) {
				stress_thrash_slab_shrink();
				stress_thrash_pagein_all_procs(page_size);
			}
			if ((stress_mwc8() & 0x7) == 0)
				stress_thrash_drop_caches();

			stress_thrash_compact_memory();

			stress_thrash_merge_memory();

			stress_thrash_zone_reclaim();

			stress_thrash_kmemleak_scan();

			stress_thrash_proc_memory();

			stress_thrash_fragment_mappings(&mmaps);

			stress_thrash_pagein_self(&mmaps, page_size);

			stress_thrash_sys_memory();

#if defined(HAVE_LINUX_MEMPOLICY_H)
			stress_thrash_move_pages(&mmaps, numa_nodes);
#endif
			stress_thrash_state("sleep");

			stress_mmaps_free(&mmaps);

			(void)sleep(1);
		}
		thrash_run = false;

#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_nodes_free(numa_mask, numa_nodes);;
#endif

		stress_thrash_state("exit");
		_exit(0);
	}
	return 0;
}

/*
 *  stress_thrash_stop()
 *	stop paging in thrash process
 */
void COLD stress_thrash_stop(void)
{
	int status;

	thrash_run = false;

	if (!thrash_pid)
		return;

	(void)shim_kill(thrash_pid, SIGALRM);
	(void)shim_waitpid(thrash_pid, &status, 0);
	if (shim_kill(thrash_pid, 0) == 0) {
		(void)shim_usleep(250000);
		(void)stress_kill_pid_wait(thrash_pid, NULL);
	}

	thrash_pid = 0;
}

#else
int CONST COLD stress_thrash_start(void)
{
	return 0;
}

void COLD stress_thrash_stop(void)
{
}
#endif
