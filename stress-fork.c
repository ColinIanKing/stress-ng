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
#include "core-madvise.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-thrash.h"

#define MIN_FORKS		(1)
#define MAX_FORKS		(16000)
#define DEFAULT_FORKS		(1)

#define MIN_VFORKS		(1)
#define MAX_VFORKS		(16000)
#define DEFAULT_VFORKS		(1)

static const stress_help_t fork_help[] = {
	{ "f N","fork N",	"start N workers spinning on fork() and exit()" },
	{ NULL,	"fork-max P",	"create P forked processes per iteration, default is 1" },
	{ NULL,	"fork-ops N",	"stop after N fork bogo operations" },
	{ NULL, "fork-pageout",	"force pageout memory resident pages" },
	{ NULL,	"fork-unmap",	"forcibly unmap unused shared library pages (dangerous)" },
	{ NULL, "fork-vm",	"enable extra virtual memory pressure" },
	{ NULL,	NULL,		NULL }
};

static const stress_help_t vfork_help[] = {
	{ NULL,	"vfork N",	"start N workers spinning on vfork() and exit()" },
	{ NULL,	"vfork-ops N",	"stop after N vfork bogo operations" },
	{ NULL,	"vfork-max P",	"create P vforked processes per iteration, default is 1" },
	{ NULL,	NULL,		NULL }
};

#define STRESS_FORK	(0)
#define STRESS_VFORK	(1)

#define STRESS_MODE_FORK_VM	(1)
#define STRESS_MODE_PAGEOUT	(2)
#define STRESS_MODE_UNMAP	(4)
#define STRESS_MODE_DONTNEED	(8)

/*
 *  stress_fork_shim_exit()
 *	perform _exit(), try and use syscall first to
 *	avoid any shared library late binding of _exit(),
 *	if the direct syscall fails do _exit() call.
 */
static inline ALWAYS_INLINE NORETURN void stress_fork_shim_exit(int status)
{
#if defined(__NR_exit) && \
    defined(HAVE_SYSCALL)
        (void)syscall(__NR_exit, status);
	/* in case __NR_exit fails, do _exit anyhow */
#endif
	_exit(status);
}

/*
 *  stress_force_bind()
 *	the child process performs various system calls via the libc
 *	shared library and this involves doing late binding on these
 *	libc functions. Since the child process has to do this many
 *	times it's useful to avoid the late binding overhead by forcing
 *	binding by calling the functions before the child uses them.
 *
 *	This could be avoided by compiling with late binding disabled
 *	via LD_FLAGS -znow however this can break on some distros due
 *	to symbol resolving ordering, so we do it using this ugly way.
 */
static void stress_force_bind(void)
{
	pid_t pid;

	pid = getpid();
#if defined(HAVE_GETPGID)
	(void)getpgid(pid);
#endif
	(void)setpgid(0, 0);
#if defined(HAVE_SYS_CAPABILITY_H)
	stress_getset_capability();
#endif
#if defined(__NR_getpid) && \
    defined(HAVE_SYSCALL)
	(void)syscall(__NR_getpid);
#endif
	(void)pid;
}

#if defined(__linux__)
/*
 *  shared libraries that stress-ng currently
 *  uses and can be potentially be force unmapped
 */
static const char * const stress_fork_shlibs[] = {
	"libacl.so",
	"libapparmor.so",
	"libbsd.so",
	"libcrypt.so",
	"libdrm.so",
	"libEGL.so",
	"libgbm.so",
	"libGLdispatch.so",
	"libGLESv2.so",
	"libgmp.so",
	"libIPSec_MB.so",
	"libjpeg.so",
	"libJudy.so",
	"libmd.so",
	"libmpfr.so",
	"libpthread.so",
	"libsctp.so",
	"libxxhash.so",
	"libz.so",
	"libkmod.so",
	"liblzma.so",
	"libwayland-server.so",
	"libzstd.so",
};

/*
 *  stress_fork_maps_reduce()
 *	force non-resident shared library pages to be unmapped
 *	to reduce vma copying. This is a Linux-only ugly hack
 *	and we expect _exit() to be called at the end of the
 *	parent and child processes.
 */
static void stress_fork_maps_reduce(const size_t page_size, const int reduce_mode)
{
	char buffer[4096];
	FILE *fp;
	const uintmax_t max_addr = UINTMAX_MAX - (UINTMAX_MAX >> 1);

	fp = fopen("/proc/self/maps", "r");
	if (!fp)
		return;

	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (fgets(buffer, sizeof(buffer), fp)) {
		uint64_t begin, end, len, offset;
		uintptr_t begin_ptr, end_ptr;
		char tmppath[1024];
		char prot[6];
		size_t i;

		tmppath[0] = '\0';
		if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		           " %5s %" SCNx64 " %*x:%*x %*d %1023s", &begin, &end, prot, &offset, tmppath) != 5) {
			continue;
		}

		if ((prot[2] != 'x') && (prot[1] != 'w'))
			continue;

		if (tmppath[0] == '\0')
			continue;

		/* Avoid vdso and vvar */
		if (strncmp("[v", tmppath, 2) == 0)
			continue;

		if ((begin > UINTPTR_MAX) || (end > UINTPTR_MAX))
			continue;

		/* Ignore bad ranges */
		if ((begin >= end) || (begin == 0) || (end == 0) || (end >= max_addr))
			continue;

		len = end - begin;

		/* Skip invalid ranges */
		if ((len < page_size) || (len > 0x80000000UL))
			continue;

		begin_ptr = (uintptr_t)begin;
		end_ptr = (uintptr_t)end;

#if defined(MADV_PAGEOUT)
		if (reduce_mode & STRESS_MODE_PAGEOUT)
			(void)madvise((void *)begin_ptr, len, MADV_PAGEOUT);
#endif

		for (i = 0; i < SIZEOF_ARRAY(stress_fork_shlibs); i++) {
			if (strstr(tmppath, stress_fork_shlibs[i])) {
				if (reduce_mode & STRESS_MODE_DONTNEED) {
					(void)madvise((void *)begin_ptr, len, MADV_DONTNEED);
				} else if (reduce_mode & STRESS_MODE_UNMAP) {
					unsigned char *vec;
					uint8_t *ptr, *unmap_start = NULL;
					size_t unmap_len = 0, j;

					vec = (unsigned char *)calloc(len / page_size, sizeof(*vec));
					if (!vec)
						continue;

					if (shim_mincore((void *)begin_ptr, (size_t)len, vec) < 0) {
						free(vec);
						continue;
					}

					/*
					 *  find longest run of pages that can be
					 *  forcibly unmapped.
					 */
					for (j = 0, ptr = (uint8_t *)begin_ptr; ptr < (uint8_t *)end_ptr; ptr += page_size, j++) {
						if (vec[j]) {
							if (unmap_len == 0) {
								unmap_start = ptr;
								unmap_len = page_size;
							} else {
								unmap_len += page_size;
							}
						} else {
							if (unmap_len != 0)
								(void)munmap((void *)unmap_start, unmap_len);
							unmap_start = NULL;
							unmap_len = 0;
						}
					}
					if (unmap_len != 0)
						(void)munmap((void *)unmap_start, unmap_len);
					free(vec);
				}
				continue;
			}
		}
	}
	(void)fclose(fp);
}
#endif

typedef struct {
	pid_t	pid;	/* Child PID */
	int	err;	/* Saved fork errno */
} fork_info_t;

/*
 *  stress_fork_fn()
 *	stress by forking and exiting using
 *	fork function fork_fn (fork or vfork)
 */
static int stress_fork_fn(
	stress_args_t *args,
	const int which,
	const uint32_t fork_max,
	const int mode)
{
	static fork_info_t info[MAX_FORKS] ALIGN64;
	NOCLOBBER uint32_t j;
#if defined(__linux__)
	NOCLOBBER bool remove_reduced = false;
#endif
	NOCLOBBER int rc = EXIT_SUCCESS;

	stress_set_oom_adjustment(args, true);
#if defined(__linux__)
	{
		int reduce_mode = mode & STRESS_MODE_PAGEOUT;

		if (mode & STRESS_MODE_UNMAP)
			reduce_mode |= STRESS_MODE_DONTNEED;

		stress_fork_maps_reduce(args->page_size, reduce_mode);
	}
#else
	(void)mode;
#endif

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	j = args->instance;
	do {
		NOCLOBBER uint32_t i, n;
		NOCLOBBER char *fork_fn_name;

		(void)shim_memset(info, 0, sizeof(info));

		for (n = 0; n < fork_max; n++, j++) {
			pid_t pid;

			switch (which) {
			case STRESS_FORK:
				fork_fn_name = "fork";
				pid = fork();
				break;
			case STRESS_VFORK:
				fork_fn_name = "vfork";
				pid = shim_vfork();
				if (pid == 0)
					stress_fork_shim_exit(0);
				break;
			default:
				/* This should not happen */
				fork_fn_name = "unknown";
				pid = -1;
				pr_fail("%s: bad fork/vfork function, aborting\n", args->name);
				errno = ENOSYS;
				break;
			}

			if (pid == 0) {
				stress_set_proc_state(args->name, STRESS_STATE_RUN);

				/*
				 *  50% of forks are very short lived exiting processes
				 */
				if (n & 1)
					stress_fork_shim_exit(0);

				if (mode & STRESS_MODE_FORK_VM) {
					int advice[6];
					size_t n_advice = 0;

					(void)shim_memset(advice, 0, sizeof(advice));

					switch (j++ & 7) {
					case 0:
#if defined(MADV_MERGEABLE)
						advice[n_advice++] = MADV_MERGEABLE;
#endif
#if defined(MADV_UNMERGEABLE)
						advice[n_advice++] = MADV_UNMERGEABLE;
#endif
#if defined(MADV_WILLNEED)
						advice[n_advice++] = MADV_WILLNEED;
#endif
#if defined(MADV_NOHUGEPAGE)
						advice[n_advice++] = MADV_NOHUGEPAGE;
#endif
#if defined(MADV_HUGEPAGE)
						advice[n_advice++] = MADV_HUGEPAGE;
#endif
#if defined(MADV_RANDOM)
						advice[n_advice++] = MADV_RANDOM;
#endif
						break;
					case 1:
#if defined(MADV_COLD)
						advice[n_advice++] = MADV_COLD;
#endif
						break;
					case 2:
#if defined(MADV_PAGEOUT)
						advice[n_advice++] = MADV_PAGEOUT;
#endif
#if defined(MADV_POPULATE_READ)
						advice[n_advice++] = MADV_POPULATE_READ;
#endif
						break;
					case 3:
#if defined(MADV_WILLNEED)
						advice[n_advice++] = MADV_WILLNEED;
#endif
#if defined(MADV_SOFT_OFFLINE)
						advice[n_advice++] = MADV_SOFT_OFFLINE;
#endif
						break;
					case 4:
#if defined(MADV_NOHUGEPAGE)
						advice[n_advice++] = MADV_NOHUGEPAGE;
#endif
#if defined(MADV_HUGEPAGE)
						advice[n_advice++] = MADV_HUGEPAGE;
#endif
						break;
					case 5:
#if defined(MADV_MERGEABLE)
						advice[n_advice++] = MADV_MERGEABLE;
#endif
#if defined(MADV_UNMERGEABLE)
						advice[n_advice++] = MADV_UNMERGEABLE;
#endif
						break;

					case 6:
						stress_ksm_memory_merge(1);
						break;
					/* cases 7 */
					default:
						break;
					}
					if (n_advice) {
						stress_madvise_pid_all_pages(getpid(), advice, n_advice);
						stress_pagein_self(args->name);
					}
				}

				/* exercise some setpgid calls before we die */
				VOID_RET(int, setpgid(0, 0));
#if defined(HAVE_GETPGID)
				{
					const pid_t my_pid = getpid();
					const pid_t my_pgid = getpgid(my_pid);

					VOID_RET(int, setpgid(my_pid, my_pgid));
				}
#else
				UNEXPECTED
#endif

				/* -ve pgid is EINVAL */
				VOID_RET(int, setpgid(0, -1));
				/* -ve pid is EINVAL */
				VOID_RET(int, setpgid(-1, 0));
				stress_fork_shim_exit(0);
			} else if (pid < 0) {
				info[n].err = errno;
			}
			info[n].pid = pid;
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
		for (i = 0; i < n; i++) {
			if (info[i].pid > 0) {
				int status;
				/* wait for child */
				(void)shim_waitpid(info[i].pid, &status, 0);
				stress_bogo_inc(args);
			}
		}

		for (i = 0; i < n; i++) {
			if ((info[i].pid < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				switch (info[i].err) {
				case EAGAIN:
				case ENOMEM:
					break;
				default:
					pr_fail("%s: %s failed, errno=%d (%s)\n", args->name,
						fork_fn_name, info[i].err, strerror(info[i].err));
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
#if defined(__APPLE__)
		/*
		 *  SIGALRMs don't get reliably delivered on OS X on
		 *  vfork so check the time in case SIGARLM was not
		 *  delivered.
		 */
		if ((which == STRESS_VFORK) && (stress_time_now() > args->time_end))
			break;
#endif
#if defined(__linux__)
		{
			int reduced_mode = mode & STRESS_MODE_PAGEOUT;
			if ((mode & STRESS_MODE_UNMAP) && !remove_reduced) {
				reduced_mode |= STRESS_MODE_UNMAP;
				remove_reduced = true;
			}
			if (reduced_mode)
				stress_fork_maps_reduce(args->page_size, reduced_mode);

		}
#endif
	} while (stress_continue(args));

	return rc;
}

/*
 *  stress_fork()
 *	stress by forking and exiting
 */
static int stress_fork(stress_args_t *args)
{
	uint32_t fork_max = DEFAULT_FORKS;
	int rc, mode = 0;
	bool fork_vm = false, fork_unmap = false, fork_pageout = false;

	(void)stress_get_setting("fork-unmap", &fork_unmap);
	(void)stress_get_setting("fork-pageout", &fork_pageout);
	(void)stress_get_setting("fork-vm", &fork_vm);

	mode = (fork_pageout ? STRESS_MODE_PAGEOUT : 0) |
	       (fork_unmap ? STRESS_MODE_UNMAP : 0) |
	       (fork_vm ? STRESS_MODE_FORK_VM : 0);

	if (!stress_get_setting("fork-max", &fork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fork_max = MAX_FORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fork_max = MIN_FORKS;
	}

	if ((mode & STRESS_MODE_FORK_VM) && fork_unmap) {
		pr_inf("%s: --fork-vm and --fork-unmap cannot be enabled "
			"at the same time, disabling --fork-unmap option\n",
			args->name);
		fork_unmap = false;
	}

	stress_force_bind();
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (fork_unmap) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			rc = stress_fork_fn(args, STRESS_FORK, fork_max, mode);
			stress_fork_shim_exit(rc);
		} else if (pid < 0) {
			rc = EXIT_FAILURE;
		} else {
			int status;

			(void)shim_waitpid(pid, &status, 0);
			if (WIFEXITED(status)) {
				rc = WEXITSTATUS(status);
			} else {
				rc = EXIT_FAILURE;
			}
		}
	} else {
		rc = stress_fork_fn(args, STRESS_FORK, fork_max, mode);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}


/*
 *  stress_vfork()
 *	stress by vforking and exiting
 */
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static int stress_vfork(stress_args_t *args)
{
	uint32_t vfork_max = DEFAULT_VFORKS;
	int rc;

	if (!stress_get_setting("vfork-max", &vfork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vfork_max = MAX_VFORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vfork_max = MIN_VFORKS;
	}

	stress_force_bind();
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	rc = stress_fork_fn(args, STRESS_VFORK, vfork_max, 0);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}
STRESS_PRAGMA_POP

static const stress_opt_t fork_opts[] = {
	{ OPT_fork_max,     "fork-max",     TYPE_ID_UINT32, MIN_FORKS, MAX_FORKS, NULL },
	{ OPT_fork_pageout, "fork-pageout", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_fork_unmap,   "fork-unmap",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_fork_vm,      "fork-vm",      TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

static const stress_opt_t vfork_opts[] = {
	{ OPT_vfork_max,    "vfork-max",    TYPE_ID_UINT32, MIN_VFORKS, MAX_VFORKS, NULL },
	END_OPT,
};

const stressor_info_t stress_fork_info = {
	.stressor = stress_fork,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = fork_opts,
	.verify = VERIFY_OPTIONAL,
	.help = fork_help
};

const stressor_info_t stress_vfork_info = {
	.stressor = stress_vfork,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = vfork_opts,
	.verify = VERIFY_OPTIONAL,
	.help = vfork_help
};
