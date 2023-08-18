// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-capabilities.h"

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
	const stress_args_t *args,
	const char *filename)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s: fork failed %d (%s), skipping concurrent access stressing\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (pid == 0) {
		/* Concurrent stressor */

		size_t j = 0;

		stress_mwc_reseed();
		shim_nice(1);
		shim_nice(1);

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

			j++;
			if (j >= SIZEOF_ARRAY(modes))
				j = 0;
			shim_sched_yield();
		} while(stress_continue(args));
		_exit(0);
	}
	return pid;
}

static void stress_access_reap(pid_t *pid)
{
	int status;

	if (*pid == -1)
		return;
	(void)shim_kill(*pid, SIGKILL);
	(void)shim_waitpid(*pid, &status, 0);
	*pid = -1;
}

/*
 *  stress_access
 *	stress access family of system calls
 */
static int stress_access(const stress_args_t *args)
{
	int fd1 = -1, fd2 = -1, ret, rc = EXIT_FAILURE;
	char filename1[PATH_MAX];
	char filename2[PATH_MAX];
	const mode_t all_mask = 0700;
	size_t i;
#if defined(HAVE_FACCESSAT)
	const int bad_fd = stress_get_bad_fd();
#endif
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);
	const char *fs_type;
	pid_t pid[2] = { -1, -1 };
	uint32_t rnd32 = stress_mwc32();
	/* 3 metrics, index 0 for parent, 1 for child, 2 for total */
	size_t metrics_size = sizeof(*metrics) * 3;
	double rate;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

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

	/* metrics in a shared page for child stats to be available to parent */
	metrics = (stress_metrics_t *)mmap(NULL, metrics_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zd bytes for metrics, skipping stressor\n",
			args->name, metrics_size);
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	metrics[0].duration = 0.0;
	metrics[0].count = 0.0;
	metrics[1].duration = 0.0;
	metrics[1].count = 0.0;

	pid[0] = stress_access_spawn(args, filename2);
	if (pid[0] >= 0) {
		pid[1] = stress_access_spawn(args, filename2);
		if (pid[1] < 0)
			stress_access_reap(&pid[0]);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			double t;
#if defined(HAVE_FACCESSAT)
			size_t j;
#endif

			ret = fchmod(fd1, modes[i].chmod_mode);
			if (CHMOD_ERR(ret)) {
				pr_fail("%s: fchmod %3.3o failed: %d (%s)%s\n",
					args->name, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
				goto unmap;
			}
			t = stress_time_now();
			ret = access(filename1, modes[i].access_mode);
			if (LIKELY(ret >= 0)) {
				metrics[0].duration = stress_time_now() - t;
				metrics[0].count += 1.0;
			} else {
				pr_fail("%s: access %3.3o on chmod mode %3.3o failed: %d (%s)%s\n",
					args->name,
					modes[i].access_mode,
					(unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
			}
#if defined(HAVE_FACCESSAT)
			t = stress_time_now();
			ret = shim_faccessat(AT_FDCWD, filename1, modes[i].access_mode, 0);
			if (LIKELY(ret >= 0)) {
				metrics[0].duration = stress_time_now() - t;
				metrics[0].count += 1.0;
			} else if (errno != ENOSYS) {
				pr_fail("%s: faccessat %3.3o on chmod mode %3.3o failed: %d (%s)%s\n",
					args->name,
					modes[i].access_mode,
					(unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
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
				pr_fail("%s: faccessat2 %3.3o on chmod mode %3.3o failed: %d (%s)%s\n",
					args->name,
					modes[i].access_mode,
					(unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
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
					pr_fail("%s: fchmod %3.3o failed: %d (%s)%s\n",
						args->name, (unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
					goto unmap;
				}
				t = stress_time_now();
				ret = access(filename1, modes[i].access_mode);
				if (UNLIKELY((ret == 0) && dont_ignore)) {
					pr_fail("%s: access %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
				} else {
					metrics[0].duration = stress_time_now() - t;
					metrics[0].count += 1.0;
				}
#if defined(HAVE_FACCESSAT)
				t = stress_time_now();
				ret = faccessat(AT_FDCWD, filename1, modes[i].access_mode,
					AT_SYMLINK_NOFOLLOW);
				if (UNLIKELY((ret == 0) && dont_ignore)) {
					pr_fail("%s: faccessat %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
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
	stress_metrics_set(args, 0, "access calls per sec", rate);

	rc = EXIT_SUCCESS;

unmap:
	(void)munmap((void *)metrics, metrics_size);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_access_reap(&pid[1]);
	stress_access_reap(&pid[0]);

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

	return rc;
}

static const stress_help_t help[] = {
	{ NULL,	"access N",	"start N workers that stress file access permissions" },
	{ NULL,	"access-ops N",	"stop after N file access bogo operations" },
	{ NULL, NULL,		NULL }
};

stressor_info_t stress_access_info = {
	.stressor = stress_access,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
