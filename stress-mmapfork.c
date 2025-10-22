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
#include "core-mmap.h"

#define MIN_MMAPFORK_BYTES     (4 * KB)
#define MAX_MMAPFORK_BYTES     (MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"mmapfork N",	    "start N workers stressing many forked mmaps/munmaps" },
	{ NULL,	"mmapfork-ops N",   "stop after N mmapfork bogo operations" },
	{ NULL,	"mmapfork-bytes N", "mmap and munmap N bytes by workers for each stress iteration" },
	{ NULL,	NULL,		    NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mmapfork_bytes, "mmapfork-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_MMAPFORK_BYTES, MAX_MMAPFORK_BYTES, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)

#define MAX_PIDS			(32)

#define MMAPFORK_FAILURE		(0x01)
#define MMAPFORK_SEGV_MMAP		(0x02)
#define MMAPFORK_SEGV_MADV_WILLNEED	(0x04)
#define MMAPFORK_SEGV_MADV_DONTNEED	(0x08)
#define MMAPFORK_SEGV_MEMSET		(0x10)
#define MMAPFORK_SEGV_MUNMAP		(0x20)
#define MMAPFORK_MASK	(MMAPFORK_SEGV_MMAP | \
			 MMAPFORK_SEGV_MADV_WILLNEED | \
			 MMAPFORK_SEGV_MADV_DONTNEED | \
			 MMAPFORK_SEGV_MEMSET | \
			 MMAPFORK_SEGV_MUNMAP)

static volatile int segv_ret;

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void NORETURN MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	_exit(segv_ret);
}

static void notrunc_strlcat(char *dst, const char *src, size_t *n)
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
static inline bool should_terminate(stress_args_t *args, const pid_t ppid)
{
	if ((shim_kill(ppid, 0) < 0) && (errno == ESRCH))
		return true;
	return !stress_continue(args);
}

#if defined(MADV_WIPEONFORK)
/*
 *  stress_memory_is_not_zero()
 *	return true if memory is non-zero
 */
static bool stress_memory_is_not_zero(const uint8_t *ptr, const size_t size)
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
 *	stress mappings + fork VM subsystem
 */
static int stress_mmapfork(stress_args_t *args)
{
	stress_pid_t s_pids[MAX_PIDS];
	struct sysinfo info;
	void *ptr;
	uint64_t segv_count = 0;
	int8_t segv_reasons = 0;
#if defined(MADV_WIPEONFORK)
	uint8_t *wipe_ptr;
	const size_t wipe_size = args->page_size;
	bool wipe_ok = false;
#endif
	bool report_size = (stress_instance_zero(args));

#if defined(MADV_WIPEONFORK)
	/*
	 *  Setup a page that should be wiped if MADV_WIPEONFORK works
	 */
	wipe_ptr = mmap(NULL, wipe_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (wipe_ptr != MAP_FAILED) {
		stress_set_vma_anon_name(wipe_ptr, wipe_size, "wipe-on-fork-data");
		(void)shim_memset(wipe_ptr, 0xff, wipe_size);
		if (shim_madvise(wipe_ptr, wipe_size, MADV_WIPEONFORK) == 0)
			wipe_ok = true;
	}
#endif


	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i, len;

		for (i = 0; i < MAX_PIDS; i++)
			s_pids[i].pid = -1;

		for (i = 0; i < MAX_PIDS; i++) {
			if (UNLIKELY(!stress_continue(args)))
				goto reap;

			s_pids[i].pid = fork();
			/* Out of resources for fork?, do a reap */
			if (s_pids[i].pid < 0)
				break;
			if (s_pids[i].pid == 0) {
				/* Child */
				const pid_t ppid = getppid();
				size_t len_total;

				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				stress_parent_died_alarm();
				(void)sched_settings_apply(true);

				if (stress_sighandler(args->name, SIGSEGV, stress_segvhandler, NULL) < 0)
					_exit(MMAPFORK_FAILURE);

				(void)shim_memset(&info, 0, sizeof(info));
				if (UNLIKELY(sysinfo(&info) < 0)) {
					pr_fail("%s: sysinfo failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					_exit(MMAPFORK_FAILURE);
				}

#if defined(MADV_WIPEONFORK)
				if (wipe_ok && (wipe_ptr != MAP_FAILED) &&
				    stress_memory_is_not_zero(wipe_ptr, wipe_size)) {
					pr_fail("%s: madvise MADV_WIPEONFORK didn't wipe page %p\n",
						args->name, (void *)wipe_ptr);
					_exit(MMAPFORK_FAILURE);
				}
#endif
				len_total = (size_t)info.freeram;
				if (!stress_get_setting("mmapfork-bytes", &len_total)) {
					if (g_opt_flags & OPT_FLAGS_MINIMIZE)
						len_total = MIN_MMAPFORK_BYTES;
					if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
						len_total = MAX_32;
				}
				if (len_total < MIN_MMAPFORK_BYTES)
					len_total = MIN_MMAPFORK_BYTES;
				if (len_total < args->page_size)
					len_total = args->page_size;

				len = (len_total / (args->instances * MAX_PIDS)) / 2;
				if (len < args->page_size)
					len = args->page_size;
				if ((i == 0) && report_size) {
					stress_usage_bytes(args, len, len_total);
				}

				segv_ret = MMAPFORK_SEGV_MMAP;
				ptr = stress_mmap_populate(NULL, len, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (ptr != MAP_FAILED) {
#if defined(MADV_WILLNEED)
					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = MMAPFORK_SEGV_MADV_WILLNEED;
					(void)shim_madvise(ptr, len, MADV_WILLNEED);
#endif
					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = MMAPFORK_SEGV_MEMSET;
					(void)shim_memset(ptr, 0, len);

#if defined(MADV_DONTNEED)
					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = MMAPFORK_SEGV_MADV_DONTNEED;
					(void)shim_madvise(ptr, len, MADV_DONTNEED);
#endif

					if (should_terminate(args, ppid))
						_exit(EXIT_SUCCESS);
					segv_ret = MMAPFORK_SEGV_MUNMAP;
					(void)stress_munmap_force(ptr, len);
				}
				_exit(EXIT_SUCCESS);
			}
			report_size = false;
		}

		/*
		 *  Wait for children to terminate
		 */
		for (i = 0; i < MAX_PIDS; i++) {
			int status;

			if (UNLIKELY(s_pids[i].pid < 0))
				continue;

			if (shim_waitpid(s_pids[i].pid, &status, 0) < 0) {
				if (UNLIKELY(errno != EINTR)) {
					pr_err("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)s_pids[i].pid, errno, strerror(errno));
				} else {
					/* Probably an SIGARLM, force reap */
					goto reap;
				}
			} else {
				s_pids[i].pid = -1;
				if (WIFEXITED(status)) {
					int masked = WEXITSTATUS(status) & MMAPFORK_MASK;

					if (masked) {
						segv_count++;
						segv_reasons |= masked;
					}
				}
			}
		}
reap:
		stress_kill_and_wait_many(args, s_pids, MAX_PIDS, SIGALRM, false);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(MADV_WIPEONFORK)
	if (wipe_ptr != MAP_FAILED)
		(void)munmap((void *)wipe_ptr, wipe_size);
#endif

	if (segv_count) {
		char buffer[1024];
		size_t n = sizeof(buffer) - 1;

		*buffer = '\0';

		if (segv_reasons & MMAPFORK_SEGV_MMAP)
			notrunc_strlcat(buffer, " mmap", &n);
		if (segv_reasons & MMAPFORK_SEGV_MADV_WILLNEED)
			notrunc_strlcat(buffer, " madvise-WILLNEED", &n);
		if (segv_reasons & MMAPFORK_SEGV_MADV_DONTNEED)
			notrunc_strlcat(buffer, " madvise-DONTNEED", &n);
		if (segv_reasons & MMAPFORK_SEGV_MEMSET)
			notrunc_strlcat(buffer, " memset", &n);
		if (segv_reasons & MMAPFORK_SEGV_MUNMAP)
			notrunc_strlcat(buffer, " munmap", &n);

		pr_dbg("%s: SIGSEGV errors: %" PRIu64 " (where:%s)\n",
			args->name, segv_count, buffer);
	}

	return EXIT_SUCCESS;
}


const stressor_info_t stress_mmapfork_info = {
	.stressor = stress_mmapfork,
	.classifier = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_mmapfork_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without sys/sysinfo_h or sysinfo() system call"
};
#endif
