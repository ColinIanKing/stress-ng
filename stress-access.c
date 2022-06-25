/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

typedef struct {
	const mode_t	chmod_mode;
	const int	access_mode;
} stress_access_t;

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
#elif defined(__NR_faccessat2)
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
 *  stress_access
 *	stress access family of system calls
 */
static int stress_access(const stress_args_t *args)
{
	int fd = -1, ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	const mode_t all_mask = 0700;
	size_t i;
#if defined(HAVE_FACCESSAT)
	const int bad_fd = stress_get_bad_fd();
#endif
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);
	const char *fs_type;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	(void)umask(0700);
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: creat failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	fs_type = stress_fs_type(filename);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
#if defined(HAVE_FACCESSAT)
			size_t j;
#endif

			ret = fchmod(fd, modes[i].chmod_mode);
			if (CHMOD_ERR(ret)) {
				pr_fail("%s: fchmod %3.3o failed: %d (%s)%s\n",
					args->name, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
				goto tidy;
			}
			ret = access(filename, modes[i].access_mode);
			if (ret < 0) {
				pr_fail("%s: access %3.3o on chmod mode %3.3o failed: %d (%s)%s\n",
					args->name,
					modes[i].access_mode,
					(unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
			}
#if defined(HAVE_FACCESSAT)
			ret = shim_faccessat(AT_FDCWD, filename, modes[i].access_mode, 0);
			if ((ret < 0) && (errno != ENOSYS)) {
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
				VOID_RET(int, shim_faccessat(AT_FDCWD, filename, modes[i].access_mode, access_flags[j]));
			}

			/*
			 *  Exercise bad dir_fd
			 */
			VOID_RET(int, shim_faccessat(bad_fd, filename, modes[i].access_mode, 0));
#else
	UNEXPECTED
#endif
#if defined(HAVE_FACCESSAT2) &&	\
    defined(AT_SYMLINK_NOFOLLOW)
			ret = faccessat2(AT_FDCWD, filename, modes[i].access_mode,
				AT_SYMLINK_NOFOLLOW);
			if ((ret < 0) && (errno != ENOSYS)) {
				pr_fail("%s: faccessat2 %3.3o on chmod mode %3.3o failed: %d (%s)%s\n",
					args->name,
					modes[i].access_mode,
					(unsigned int)modes[i].chmod_mode,
					errno, strerror(errno), fs_type);
			}
			/*
			 *  Exercise bad dir_fd
			 */
			VOID_RET(int, faccessat2(bad_fd, filename, modes[i].access_mode,
				AT_SYMLINK_NOFOLLOW));
#else
			/* UNEXPECTED */
#endif
			if (modes[i].access_mode != 0) {
				const mode_t chmod_mode = modes[i].chmod_mode ^ all_mask;
				const bool s_ixusr = chmod_mode & S_IXUSR;
				const bool dont_ignore = !(is_root && s_ixusr);

				ret = fchmod(fd, chmod_mode);
				if (CHMOD_ERR(ret)) {
					pr_fail("%s: fchmod %3.3o failed: %d (%s)%s\n",
						args->name, (unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
					goto tidy;
				}
				ret = access(filename, modes[i].access_mode);
				if ((ret == 0) && dont_ignore) {
					pr_fail("%s: access %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
				}
#if defined(HAVE_FACCESSAT)
				ret = faccessat(AT_FDCWD, filename, modes[i].access_mode,
					AT_SYMLINK_NOFOLLOW);
				if ((ret == 0) && dont_ignore) {
					pr_fail("%s: faccessat %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)%s\n",
						args->name,
						modes[i].access_mode,
						(unsigned int)chmod_mode,
						errno, strerror(errno), fs_type);
				}
#else
	UNEXPECTED
#endif
			}
		}
		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (fd >= 0) {
		(void)fchmod(fd, S_IRUSR | S_IWUSR);
		(void)close(fd);
	}
	(void)shim_unlink(filename);
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
