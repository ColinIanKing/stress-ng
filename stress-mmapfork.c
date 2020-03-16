/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"mmapfork N",	  "start N workers stressing many forked mmaps/munmaps" },
	{ NULL,	"mmapfork-ops N", "stop after N mmapfork bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_SYS_SYSINFO_H) && defined(HAVE_SYSINFO)

#define MAX_PIDS		(32)

#define _EXIT_FAILURE			(0x01)
#define _EXIT_SEGV_MMAP			(0x02)
#define _EXIT_SEGV_MADV_WILLNEED	(0x04)
#define _EXIT_SEGV_MADV_DONTNEED	(0x08)
#define _EXIT_SEGV_MEMSET		(0x10)
#define _EXIT_SEGV_MUNMAP		(0x20)
#define _EXIT_MASK	(_EXIT_SEGV_MMAP | \
			 _EXIT_SEGV_MADV_WILLNEED | \
			 _EXIT_SEGV_MADV_DONTNEED | \
			 _EXIT_SEGV_MEMSET | \
			 _EXIT_SEGV_MUNMAP)

static volatile int segv_ret;

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	_exit(segv_ret);
}

static void __strlcat(char *dst, char *src, size_t *n)
{
	const size_t ln = strlen(src);

	if (*n <= ln)
		return;

	(void)shim_strlcat(dst, src, *n);
	*n -= ln;
}

/*
 *  should_terminate()
 *	check that parent hasn't been OOM'd or it is time to die
 */
static inline bool should_terminate(const stress_args_t *args, const pid_t ppid)
{
	if ((kill(ppid, 0) < 0) && (errno == ESRCH))
		return true;
	return !keep_stressing();
}

#if defined(MADV_WIPEONFORK)
/*
 *  stress_memory_is_not_zero()
 *	return true if memory is non-zero
 */
static bool stress_memory_is_not_zero(uint8_t *ptr, const size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		if (ptr[i])
			return true;
	return false;
}
#endif

/*
 *  stress_mmapfork()
 *	stress mappings + fork VM subystem
 */
static int stress_mmapfork(const stress_args_t *args)
{
	pid_t pids[MAX_PIDS];
	struct sysinfo info;
	void *ptr;
	uint64_t segv_count = 0;
	int8_t segv_reasons = 0;
#if defined(MADV_WIPEONFORK)
	uint8_t *wipe_ptr;
	const size_t wipe_size = args->page_size;
	bool wipe_ok = false;
#endif

#if defined(MADV_WIPEONFORK)
	/*
	 *  Setup a page that should be wiped if MADV_WIPEONFORK works
	 */
	wipe_ptr = mmap(NULL, wipe_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (wipe_ptr != MAP_FAILED) {
		(void)memset(wipe_ptr, 0xff, wipe_size);
		if (shim_madvise(wipe_ptr, wipe_size, MADV_WIPEONFORK) == 0)
			wipe_ok = true;
	}
#endif

	do {
		size_t i, len;

		for (i = 0; i < MAX_PIDS; i++)
			pids[i] = -1;

		for (i = 0; i < MAX_PIDS; i++) {
			if (!keep_stressing())
				goto reap;

			pids[i] = fork();
			/* Out of resources for fork?, do a reap */
			if (pids[i] < 0)
				break;
			if (pids[i] == 0) {
				/* Child */
				const pid_t ppid = getppid();

				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();

				if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
					_exit(_EXIT_FAILURE);

				if (sysinfo(&info) < 0) {
					pr_fail_err("sysinfo");
					_exit(_EXIT_FAILURE);
				}
#if defined(MADV_WIPEONFORK)
				if (wipe_ok && (wipe_ptr != MAP_FAILED) &&
				    stress_memory_is_not_zero(wipe_ptr, wipe_size)) {
					pr_fail("%s: madvise MADV_WIPEONFORK didn't wipe page %p\n",
						args->name, wipe_ptr);
					_exit(_EXIT_FAILURE);
				}
#endif

				len = ((size_t)info.freeram / (args->num_instances * MAX_PIDS)) / 2;
				segv_ret = _EXIT_SEGV_MMAP;
				ptr = mmap(NULL, len, PROT_READ | PROT_WRITE,
					MAP_POPULATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (ptr != MAP_FAILED) {
					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = _EXIT_SEGV_MADV_WILLNEED;
					(void)shim_madvise(ptr, len, MADV_WILLNEED);

					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = _EXIT_SEGV_MEMSET;
					(void)memset(ptr, 0, len);

					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = _EXIT_SEGV_MADV_DONTNEED;
					(void)shim_madvise(ptr, len, MADV_DONTNEED);

					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = _EXIT_SEGV_MUNMAP;
					(void)munmap(ptr, len);
				}
				_exit(EXIT_SUCCESS);
			}
			(void)setpgid(pids[i], g_pgrp);
		}

		/*
		 *  Wait for children to terminate
		 */
		for (i = 0; i < MAX_PIDS; i++) {
			int status;

			if (UNLIKELY(pids[i] < 0))
				continue;

			if (shim_waitpid(pids[i], &status, 0) < 0) {
				if (errno != EINTR) {
					pr_err("%s: waitpid errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				} else {
					/* Probably an SIGARLM, force reap */
					goto reap;
				}
			} else {
				pids[i] = -1;
				if (WIFEXITED(status)) {
					int masked = WEXITSTATUS(status) & _EXIT_MASK;

					if (masked) {
						segv_count++;
						segv_reasons |= masked;
					}
				}
			}
		}
reap:
		/*
		 *  Force child death and reap
		 */
		for (i = 0; i < MAX_PIDS; i++) {
			int status;

			if (UNLIKELY(pids[i] < 0))
				continue;
			(void)kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
		inc_counter(args);
	} while (keep_stressing());

#if defined(MADV_WIPEONFORK)
	if (wipe_ptr != MAP_FAILED)
		munmap(wipe_ptr, wipe_size);
#endif

	if (segv_count) {
		char buffer[1024];
		size_t n = sizeof(buffer) - 1;

		*buffer = '\0';

		if (segv_reasons & _EXIT_SEGV_MMAP)
			__strlcat(buffer, " mmap", &n);
		if (segv_reasons & _EXIT_SEGV_MADV_WILLNEED)
			__strlcat(buffer, " madvise-WILLNEED", &n);
		if (segv_reasons & _EXIT_SEGV_MADV_DONTNEED)
			__strlcat(buffer, " madvise-DONTNEED", &n);
		if (segv_reasons & _EXIT_SEGV_MEMSET)
			__strlcat(buffer, " memset", &n);
		if (segv_reasons & _EXIT_SEGV_MUNMAP)
			__strlcat(buffer, " munmap", &n);

		pr_dbg("%s: SIGSEGV errors: %" PRIu64 " (where:%s)\n",
			args->name, segv_count, buffer);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_mmapfork_info = {
	.stressor = stress_mmapfork,
	.class = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_mmapfork_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.help = help
};
#endif
