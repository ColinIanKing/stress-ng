/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
 *  stress_pagein_handler()
 *	jmp back to stress_pagein_self on signal
 */
static void MLOCKED_TEXT stress_pagein_handler(int signum)
{
	(void)signum;

	if (jmp_env_set)
		siglongjmp(jmp_env, 1);
}

/*
 *  stress_thrash_state()
 *	set name of thrasher child process
 */
static void stress_thrash_state(const char *state)
{
	stress_set_proc_state_str("thrash", state);
}

/*
 *  stress_pagein_self()
 *	force pages into memory for current process
 */
int stress_pagein_self(const char *name)
{
	char buffer[4096];
	int rc = 0, ret;
	NOCLOBBER FILE *fpmap = NULL;
	const size_t page_size = stress_get_page_size();
	struct sigaction bus_action, segv_action;

	jmp_env_set = false;

	VOID_RET(int, stress_sighandler(name, SIGBUS, stress_pagein_handler, &bus_action));
	VOID_RET(int, stress_sighandler(name, SIGSEGV, stress_pagein_handler, &segv_action));

	ret = sigsetjmp(jmp_env, 1);
	if (ret == 1)
		goto err;

	fpmap = fopen("/proc/self/maps", "r");
	if (!fpmap)
		return -errno;

	stress_thrash_state("pagein");
	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (fgets(buffer, sizeof(buffer), fpmap)) {
		uintmax_t begin, end, len;
		uintptr_t off;
		char tmppath[1024];
		char prot[6];

		if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		           " %5s %*x %*x:%*x %*d %1023s", &begin, &end, prot, tmppath) != 4) {
			continue;
		}

		/* Avoid VDSO etc.. */
		if (strncmp("[v", tmppath, 2) == 0)
			continue;

		/* ignore non-readable or non-private mappings */
		if (prot[0] != 'r')
			continue;
		len = end - begin;

		/* Ignore bad range */
		if ((begin >= end) || (len == 0) || (begin == 0))
			continue;
		/* Skip huge ranges more than 2GB */
		if (len > 0x80000000UL)
			continue;

		for (off = begin; off < end; off += page_size) {
			volatile uint8_t *ptr = (uint8_t *)off;
			const uint8_t value = *ptr;

			if (prot[1] == 'w')
				*ptr = value;
		}
	}

err:
	if (fpmap)
		(void)fclose(fpmap);

	jmp_env_set = false;
	/* Restore action */
	(void)sigaction(SIGBUS, &bus_action, NULL);
	(void)sigaction(SIGSEGV, &segv_action, NULL);

	return rc;
}

/*
 *  stress_pagein_proc()
 *	force pages into memory for a given process
 */
static int stress_pagein_proc(const pid_t pid)
{
	char path[PATH_MAX];
	char buffer[4096];
	int fdmem, rc = 0;
	FILE *fpmap;
	const size_t page_size = stress_get_page_size();

	if ((pid == parent_pid) || (pid == getpid()))
		return 0;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX" /mem", (intmax_t)pid);
	fdmem = open(path, O_RDONLY);
	if (fdmem < 0)
		return -errno;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX" /maps", (intmax_t)pid);
	fpmap = fopen(path, "r");
	if (!fpmap) {
		rc = -errno;
		goto exit_fdmem;
	}

	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (thrash_run && fgets(buffer, sizeof(buffer), fpmap)) {
		uintmax_t begin, end, len;
		uintptr_t off;
		char tmppath[1024];
		char prot[6];

		if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		           " %5s %*x %*x:%*x %*d %1023s", &begin, &end, prot, tmppath) != 4)
			continue;

		/* ignore non-readable or non-private mappings */
		if ((prot[0] != 'r') && (prot[3] != 'p'))
			continue;
		len = end - begin;

		/* Ignore bad range */
		if ((begin >= end) || (len == 0) || (begin == 0))
			continue;
		/* Skip huge ranges more than 2GB */
		if (len > 0x80000000UL)
			continue;

		for (off = begin; thrash_run && (off < end); off += page_size) {
			unsigned long data;
			off_t pos;

			pos = lseek(fdmem, (off_t)off, SEEK_SET);
			if (pos != (off_t)off)
				continue;
			VOID_RET(ssize_t, read(fdmem, &data, sizeof(data)));
		}
	}

	(void)fclose(fpmap);
exit_fdmem:
	(void)close(fdmem);

	return rc;
}

/*
 *  stress_compact_memory()
 *	trigger memory compaction, Linux only
 */
static inline void stress_compact_memory(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("compact");
	VOID_RET(ssize_t, stress_system_write("/proc/sys/vm/compact_memory", "1", 1));
#endif
}

/*
 *  stress_zone_reclaim()
 *	trigger reclaim when zones run out of memory
 */
static inline void stress_zone_reclaim(void)
{
#if defined(__linux__)
	char mode[2];

	if (!thrash_run)
		return;

	mode[0] = '0' + (stress_mwc8() & 7);
	mode[1] = '\0';

	stress_thrash_state("reclaim");
	VOID_RET(ssize_t, stress_system_write("/proc/sys/vm/zone_reclaim_mode", mode, 1));
#endif
}

/*
 *  stress_kmemleak_scan()
 *	trigger kernel memory leak scan
 */
static inline void stress_kmemleak_scan(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("scan");
	VOID_RET(ssize_t, stress_system_write("/sys/kernel/debug/kmemleak", "scan", 4));
#endif
}

/*
 *  stress_slab_shrink()
 *	shrink slabs to help release some memory
 */
static inline void stress_slab_shrink(void)
{
	DIR *dir;
	struct dirent *d;
	static const char slabpath[] = "/sys/kernel/slab";

	/*
	 *  older shrink interface, may fail
	 */
	VOID_RET(ssize_t, stress_system_write("/sys/kernel/slab/cache/shrink", "1", 1));

	dir = opendir(slabpath);
	if (!dir)
		return;

	stress_thrash_state("shrink");
	/*
	 *  shrink all slabs
	 */
	while ((d = readdir(dir)) != NULL) {
		if (isalpha((int)d->d_name[0]))  {
			char path[PATH_MAX];

			(void)snprintf(path, sizeof(path), "%s/%s", slabpath, d->d_name);
			VOID_RET(ssize_t, stress_system_write(path, "1", 1));
		}
	}
	(void)closedir(dir);
}

/*
 *  stress_drop_caches()
 *	drop caches
 */
static inline void stress_drop_caches(void)
{
#if defined(__linux__)
	static int method = 0;
	char str[3];

	str[0] = '1' + (char)method;
	str[1] = '\0';

	stress_thrash_state("dropcache");
	VOID_RET(ssize_t, stress_system_write("/proc/sys/vm/drop_caches", str, 1));
	if (method++ >= 2)
		method = 0;
#endif
}

/*
 *  stress_merge_memory()
 *	trigger ksm memory merging, Linux only
 */
static inline void stress_merge_memory(void)
{
#if defined(__linux__)
	if (!thrash_run)
		return;

	stress_thrash_state("merge");
	VOID_RET(ssize_t, stress_system_write("/proc/sys/mm/ksm/run", KSM_RUN_MERGE, 1));
#endif
}

/*
 *  stress_pagein_all_procs()
 *	force pages into memory for all processes
 */
static int stress_pagein_all_procs(void)
{
	DIR *dp;
	struct dirent *d;

	dp = opendir("/proc");
	if (!dp)
		return -1;

	while (thrash_run && ((d = readdir(dp)) != NULL)) {
		intmax_t pid;

		if (isdigit(d->d_name[0]) &&
		    sscanf(d->d_name, "%" SCNdMAX, &pid) == 1) {
			char procpath[128];
			struct stat statbuf;

			(void)snprintf(procpath, sizeof(procpath), "/proc/%" PRIdMAX, pid);
			if (shim_stat(procpath, &statbuf) < 0)
				continue;

			if (statbuf.st_uid == 0)
				continue;

			stress_pagein_proc((pid_t)pid);
		}
	}
	(void)closedir(dp);

	return 0;
}

/*
 *  stress_thrash_start()
 *	start paging in thrash process
 */
int stress_thrash_start(void)
{
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
		pr_err("thrash background process failed to fork: %d (%s)\n",
			errno, strerror(errno));
		return -1;
	} else if (thrash_pid == 0) {
#if defined(SCHED_RR)
		VOID_RET(int, stress_set_sched(getpid(), SCHED_RR, 10, true));
#endif
		stress_thrash_state("init");
		if (stress_sighandler("main", SIGALRM, stress_thrash_handler, NULL) < 0)
			_exit(0);

		while (thrash_run) {
			if ((stress_mwc8() & 0x3) == 0) {
				stress_slab_shrink();
				stress_pagein_all_procs();
			}
			if ((stress_mwc8() & 0x7) == 0) {
				stress_drop_caches();
			}
			stress_compact_memory();

			stress_merge_memory();

			stress_zone_reclaim();

			stress_kmemleak_scan();

			stress_thrash_state("sleep");
			(void)sleep(1);
		}
		thrash_run = false;
		stress_thrash_state("exit");
		_exit(0);
	}
	return 0;
}

/*
 *  stress_thrash_stop()
 *	stop paging in thrash process
 */
void stress_thrash_stop(void)
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
int stress_thrash_start(void)
{
	return 0;
}

void stress_thrash_stop(void)
{
}

int stress_pagein_self(const char *name)
{
	(void)name;

	return 0;
}
#endif
