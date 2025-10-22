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
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-mmap.h"

#define STRESS_ACCESS_PROCS	(2)

typedef struct {
	const mode_t	chmod_mode;
	const int	access_mode;
} stress_access_t;

static stress_metrics_t *metrics;

static const stress_access_t modes[] = {
#if defined(S_IRUSR) &&	\
    defined(R_OK)
	{ S_IRUSR, R_OK },
#endif
#if defined(S_IWUSR) &&	\
    defined(W_OK)
	{ S_IWUSR, W_OK },
#endif
#if defined(S_IXUSR) &&	\
    defined(X_OK)
	{ S_IXUSR, X_OK },
#endif

#if defined(S_IRUSR) &&	\
    defined(F_OK)
	{ S_IRUSR, F_OK },
#endif
#if defined(S_IWUSR) &&	\
    defined(F_OK)
	{ S_IWUSR, F_OK },
#endif
#if defined(S_IXUSR) &&	\
    defined(F_OK)
	{ S_IXUSR, F_OK },
#endif

#if defined(S_IRUSR) &&	\
    defined(R_OK) &&	\
    defined(S_IWUSR) &&	\
    defined(W_OK)
	{ S_IRUSR | S_IWUSR, R_OK | W_OK },
#endif
#if defined(S_IRUSR) &&	\
    defined(R_OK) &&	\
    defined(S_IXUSR) &&	\
    defined(X_OK)
	{ S_IRUSR | S_IXUSR, R_OK | X_OK },
#endif
#if defined(S_IWUSR) &&	\
    defined(W_OK) &&	\
    defined(S_IXUSR) &&	\
    defined(X_OK)
	{ S_IRUSR | S_IWUSR, R_OK | W_OK },
#endif

#if defined(S_IRUSR) &&	\
    defined(F_OK) &&	\
    defined(S_IWUSR)
	{ S_IRUSR | S_IWUSR, F_OK },
#endif
#if defined(S_IRUSR) &&	\
    defined(F_OK) &&	\
    defined(S_IXUSR)
	{ S_IRUSR | S_IXUSR, F_OK },
#endif
#if defined(S_IWUSR) &&	\
    defined(F_OK) &&	\
    defined(S_IXUSR)
	{ S_IRUSR | S_IWUSR, F_OK },
#endif
};

#if defined(HAVE_FACCESSAT)
static const int access_flags[] = {
	0,
#if defined(AT_EACCESS)
	AT_EACCESS,
#endif
#if defined(AT_SYMLINK_NOFOLLOW)
	AT_SYMLINK_NOFOLLOW,
#endif
#if defined(AT_EMPTY_PATH)
	AT_EMPTY_PATH,
#endif
	~0,
};
#endif

/*
 *  BSD systems can return EFTYPE which we can ignore
 *  as a "known" error on invalid chmod mode bits
 */
#if defined(EFTYPE)
#define CHMOD_ERR(x) ((x) && (errno != EFTYPE))
#else
#define CHMOD_ERR(x) (x)
#endif

/*
 *  shim_faccessat()
 *	try to use the faccessat2 system call directly rather than libc as
 *	this calls faccessat and/or fstatat. If we don't have the system
 *	call number than revert to the libc implementation
 */
#if defined(HAVE_FACCESSAT)
static int shim_faccessat(int dir_fd, const char *pathname, int mode, int flags)
{
#if defined(HAVE_FACCESSAT2)
	return faccessat2(dir_fd, pathname, mode, flags);
#elif defined(__NR_faccessat2) &&	\
      defined(HAVE_SYSCALL)
	int ret;

	ret = (int)syscall(__NR_faccessat2, dir_fd, pathname, mode, flags);
	if ((ret < 0) && (errno != ENOSYS))
		return ret;
	else
		return faccessat(dir_fd, pathname, mode, flags);
#else
	return faccessat(dir_fd, pathname, mode, flags);
#endif
}
#endif

/*
 *  stress_access_spawn()
 *	start a child process that sets the file mode and then
 *	exercises acesss on it. There are two of these running
 *	with a lower priority than the main process and these
 *	two processes exercise chmod/access contention on a
 *	single file
 */
static pid_t stress_access_spawn(
	stress_args_t *args,
	const char *filename,
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid)
{
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		pr_inf_skip("%s: fork failed %d (%s), skipping concurrent access stressing\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (s_pid->pid == 0) {
		/* Concurrent stressor */
		size_t j = 0;

		stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
		s_pid->pid = getpid();
		stress_sync_start_wait_s_pid(s_pid);
		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		stress_mwc_reseed();
		(void)shim_nice(1);
		(void)shim_nice(1);

		do {
			double t;
			const size_t i = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(modes));
			int ret;

			ret = chmod(filename, modes[i].chmod_mode);
			switch (ret) {
			case EACCES:
			case EFAULT:
			case EIO:
			case ELOOP:
			case ENAMETOOLONG:
			case ENOENT:
			case ENOTDIR:
			case EPERM:
			case EROFS:
				_exit(0);
			default:
				break;
			}

			t = stress_time_now();
			VOID_RET(int, access(filename, modes[i].access_mode));
			metrics[1].duration += stress_time_now() - t;
			metrics[1].count += 1.0;

			t = stress_time_now();
			VOID_RET(int, access(filename, modes[j].access_mode));
			metrics[1].duration += stress_time_now() - t;
			metrics[1].count += 1.0;

			if (g_opt_flags & OPT_FLAGS_AGGRESSIVE) {
				t = stress_time_now();
				VOID_RET(int, access(filename, modes[0].access_mode));
				VOID_RET(int, access(filename, modes[j].access_mode));
				VOID_RET(int, access(filename, modes[0].access_mode));
				VOID_RET(int, access(filename, modes[j].access_mode));
				metrics[1].duration += stress_time_now() - t;
				metrics[1].count += 4.0;
			} else {
				(void)shim_sched_yield();
			}

			j++;
			if (UNLIKELY(j >= SIZEOF_ARRAY(modes)))
				j = 0;
		} while (stress_continue(args));
		_exit(0);
	} else {
		stress_sync_start_s_pid_list_add(s_pids_head, s_pid);
	}
	return s_pid->pid;
}

static void stress_access_reap(stress_pid_t *s_pid)
{
	if (s_pid->pid == -1)
		return;
	(void)stress_kill_pid_wait(s_pid->pid, NULL);
	s_pid->pid = -1;
}

/*
 *  stress_access
 *	stress access family of system calls
 */
static int stress_access(stress_args_t *args)
{
	int fd1 = -1, fd2 = -1, ret, rc = EXIT_FAILURE;
	char filename1[PATH_MAX];
	char filename2[PATH_MAX];
	const mode_t all_mask = 0700;
#if defined(HAVE_FACCESSAT)
	const int bad_fd = stress_get_bad_fd();
#endif
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);
	const char *fs_type;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	uint32_t rnd32 = stress_mwc32();
	/* 3 metrics, index 0 for parent, 1 for child, 2 for total */
	size_t i, metrics_size = sizeof(*metrics) * 3;
	double rate;
	bool report_chmod_error = true;
	static const char * const ignore_chmod_fs[] = {
		"exfat",
		"msdos",
		"hfs",
		"fuse"
	};

	s_pids = stress_sync_s_pids_mmap(STRESS_ACCESS_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, STRESS_ACCESS_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_ACCESS_PROCS);
		return stress_exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename1, sizeof(filename1), rnd32);
	(void)stress_temp_filename_args(args,
		filename2, sizeof(filename2), rnd32 + 1);

	(void)umask(0700);
	if ((fd1 = creat(filename1, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: creat on %s failed, errno=%d (%s)\n",
			args->name, filename1, errno, strerror(errno));
		goto tidy;
	}
	if ((fd2 = creat(filename2, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: creat on %s failed, errno=%d (%s)\n",
			args->name, filename2, errno, strerror(errno));
		goto tidy;
	}

	fs_type = stress_get_fs_type(filename1);
	/*
	 * Some file systems can't do some forms of chmod correctly
	 * due to limited mode bits in the underlying file system,
	 * so silently ignore error reports on these
	 */
	for (i = 0; i < SIZEOF_ARRAY(ignore_chmod_fs); i++) {
		if (strcmp(fs_type, ignore_chmod_fs[i]) == 0) {
			report_chmod_error = false;
			break;
		}
	}

	/* metrics in a shared page for child stats to be available to parent */
	metrics = (stress_metrics_t *)stress_mmap_populate(NULL,
			metrics_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes for metrics%s, skipping stressor\n",
			args->name, metrics_size, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}
	stress_set_vma_anon_name(metrics, metrics_size, "metrics");
	stress_zero_metrics(metrics, 2);

	stress_sync_start_init(&s_pids[0]);
	stress_sync_start_init(&s_pids[1]);

	if (stress_access_spawn(args, filename2, &s_pids_head, &s_pids[0]) >= 0) {
		if (stress_access_spawn(args, filename2, &s_pids_head, &s_pids[1]) < 0) {
			stress_access_reap(&s_pids[0]);
			pr_inf_skip("%s: cannot spawn access child process, skipping stressor\n", args->name);
			rc = EXIT_NO_RESOURCE;
			goto unmap;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			double t;
#if defined(HAVE_FACCESSAT)
			size_t j;
#endif

			ret = fchmod(fd1, modes[i].chmod_mode);
			if (UNLIKELY(CHMOD_ERR(ret))) {
				pr_fail("%s: fchmod %3.3o failed, errno=%d (%s)%s\n",
					args->name, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
				rc = EXIT_FAILURE;
				goto unmap;
			}
			t = stress_time_now();
			ret = access(filename1, modes[i].access_mode);
			if (LIKELY(ret >= 0)) {
				metrics[0].duration = stress_time_now() - t;
				metrics[0].count += 1.0;
			} else {
				if (report_chmod_error) {
					pr_fail("%s: access %3.3o on chmod mode %3.3o failed, errno=%d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)modes[i].chmod_mode,
						errno, strerror(errno), fs_type);
					rc = EXIT_FAILURE;
					goto unmap;
				}
			}
#if defined(HAVE_FACCESSAT)
			t = stress_time_now();
			ret = shim_faccessat(AT_FDCWD, filename1, modes[i].access_mode, 0);
			if (LIKELY(ret >= 0)) {
				metrics[0].duration = stress_time_now() - t;
				metrics[0].count += 1.0;
			} else if (errno != ENOSYS) {
				if (report_chmod_error) {
					pr_fail("%s: faccessat %3.3o on chmod mode %3.3o failed, errno=%d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)modes[i].chmod_mode,
						errno, strerror(errno), fs_type);
					rc = EXIT_FAILURE;
					goto unmap;
				}
			}

			/*
			 *  Exercise various flags, use the direct system call as preferred
			 *  first choice if it is possible.
			 */
			for (j = 0; j < SIZEOF_ARRAY(access_flags); j++) {
				VOID_RET(int, shim_faccessat(AT_FDCWD, filename1, modes[i].access_mode, access_flags[j]));
			}

			/*
			 *  Exercise bad dir_fd
			 */
			VOID_RET(int, shim_faccessat(bad_fd, filename1, modes[i].access_mode, 0));
#else
			UNEXPECTED
#endif
#if defined(HAVE_FACCESSAT2) &&	\
    defined(AT_SYMLINK_NOFOLLOW)
			t = stress_time_now();
			ret = faccessat2(AT_FDCWD, filename1, modes[i].access_mode,
				AT_SYMLINK_NOFOLLOW);
			if (LIKELY(ret >= 0)) {
				metrics[0].duration = stress_time_now() - t;
				metrics[0].count += 1.0;
			} else if (errno != ENOSYS) {
				if (report_chmod_error) {
					pr_fail("%s: faccessat2 %3.3o on chmod mode %3.3o failed, errno=%d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)modes[i].chmod_mode,
						errno, strerror(errno), fs_type);
					rc = EXIT_FAILURE;
					goto unmap;
				}
			}
			/*
			 *  Exercise bad dir_fd
			 */
			VOID_RET(int, faccessat2(bad_fd, filename1, modes[i].access_mode,
				AT_SYMLINK_NOFOLLOW));
#else
			/* UNEXPECTED */
#endif
			if (modes[i].access_mode != 0) {
				const mode_t chmod_mode = modes[i].chmod_mode ^ all_mask;
				const bool s_ixusr = chmod_mode & S_IXUSR;
				const bool dont_ignore = !(is_root && s_ixusr);

				ret = fchmod(fd1, chmod_mode);
				if (CHMOD_ERR(ret)) {
					pr_fail("%s: fchmod %3.3o failed, errno=%d (%s)%s\n",
						args->name, (unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
					rc = EXIT_FAILURE;
					goto unmap;
				}
				t = stress_time_now();
				errno = 0;
				ret = access(filename1, modes[i].access_mode);
				if (UNLIKELY((ret == 0) && dont_ignore)) {
					if (report_chmod_error) {
						pr_fail("%s: access %3.3o on chmod mode %3.3o was ok (not expected), errno=%d (%s)%s\n",
							args->name,
							modes[i].access_mode,
							(unsigned int)chmod_mode,
							errno, strerror(errno), fs_type);
						rc = EXIT_FAILURE;
						goto unmap;
					}
				} else {
					metrics[0].duration = stress_time_now() - t;
					metrics[0].count += 1.0;
				}
#if defined(HAVE_FACCESSAT)
				t = stress_time_now();
				errno = 0;
				ret = faccessat(AT_FDCWD, filename1, modes[i].access_mode,
					AT_SYMLINK_NOFOLLOW);
				if (UNLIKELY((ret == 0) && dont_ignore)) {
					if (report_chmod_error) {
						pr_fail("%s: faccessat %3.3o on chmod mode %3.3o was ok (not expected), errno=%d (%s)%s\n",
							args->name,
							modes[i].access_mode,
							(unsigned int)chmod_mode,
							errno, strerror(errno), fs_type);
						rc = EXIT_FAILURE;
						goto unmap;
					}
				} else {
					metrics[0].duration = stress_time_now() - t;
					metrics[0].count += 1.0;
				}
#else
	UNEXPECTED
#endif
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	metrics[2].duration = metrics[0].duration + metrics[1].duration;
	metrics[2].count = metrics[0].count + metrics[1].count;

	rate = (metrics[2].duration > 0.0) ? metrics[2].count / metrics[2].duration : 0.0;
	stress_metrics_set(args, 0, "access calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	rc = EXIT_SUCCESS;

unmap:
	(void)munmap((void *)metrics, metrics_size);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_access_reap(&s_pids[1]);
	stress_access_reap(&s_pids[0]);

	if (fd2 >= 0) {
		(void)fchmod(fd2, S_IRUSR | S_IWUSR);
		(void)close(fd2);
	}
	if (fd1 >= 0) {
		(void)fchmod(fd1, S_IRUSR | S_IWUSR);
		(void)close(fd1);
	}
	(void)shim_unlink(filename2);
	(void)shim_unlink(filename1);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_sync_s_pids_munmap(s_pids, STRESS_ACCESS_PROCS);

	return rc;
}

static const stress_help_t help[] = {
	{ NULL,	"access N",	"start N workers that stress file access permissions" },
	{ NULL,	"access-ops N",	"stop after N file access bogo operations" },
	{ NULL, NULL,		NULL }
};

const stressor_info_t stress_access_info = {
	.stressor = stress_access,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
