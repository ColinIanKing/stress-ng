/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

typedef struct {
	const mode_t	chmod_mode;
	const int	access_mode;
} access_t;

static const access_t modes[] = {
#if defined(S_IRUSR) && defined(R_OK)
	{ S_IRUSR, R_OK },
#endif
#if defined(S_IWUSR) && defined(W_OK)
	{ S_IWUSR, W_OK },
#endif
#if defined(S_IXUSR) && defined(X_OK)
	{ S_IXUSR, X_OK },
#endif

#if defined(S_IRUSR) && defined(F_OK)
	{ S_IRUSR, F_OK },
#endif
#if defined(S_IWUSR) && defined(F_OK)
	{ S_IWUSR, F_OK },
#endif
#if defined(S_IXUSR) && defined(F_OK)
	{ S_IXUSR, F_OK },
#endif

#if defined(S_IRUSR) && defined(R_OK) && \
    defined(S_IWUSR) && defined(W_OK)
	{ S_IRUSR | S_IWUSR, R_OK | W_OK },
#endif
#if defined(S_IRUSR) && defined(R_OK) && \
    defined(S_IXUSR) && defined(X_OK)
	{ S_IRUSR | S_IXUSR, R_OK | X_OK },
#endif
#if defined(S_IWUSR) && defined(W_OK) && \
    defined(S_IXUSR) && defined(X_OK)
	{ S_IRUSR | S_IWUSR, R_OK | W_OK },
#endif

#if defined(S_IRUSR) && defined(F_OK) && \
    defined(S_IWUSR)
	{ S_IRUSR | S_IWUSR, F_OK },
#endif
#if defined(S_IRUSR) && defined(F_OK) && \
    defined(S_IXUSR)
	{ S_IRUSR | S_IXUSR, F_OK },
#endif
#if defined(S_IWUSR) && defined(F_OK) && \
    defined(S_IXUSR)
	{ S_IRUSR | S_IWUSR, F_OK },
#endif
};

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
 *  stress_access
 *	stress access family of system calls
 */
static int stress_access(const args_t *args)
{
	int fd = -1, ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	const mode_t all_mask = 0700;
	size_t i;
	const bool is_root = (geteuid() == 0);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	(void)umask(0700);
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("creat");
		goto tidy;
	}

	do {
		for (i = 0; i < SIZEOF_ARRAY(modes); i++) {
			ret = fchmod(fd, modes[i].chmod_mode);
			if (CHMOD_ERR(ret)) {
				pr_err("%s: fchmod %3.3o failed: %d (%s)\n",
					args->name, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno));
				goto tidy;
			}
			ret = access(filename, modes[i].access_mode);
			if (ret < 0) {
				pr_fail("%s: access %3.3o on chmod mode %3.3o failed: %d (%s)\n",
					args->name,
					modes[i].access_mode, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno));
			}
#if defined(HAVE_FACCESSAT)
			ret = faccessat(AT_FDCWD, filename, modes[i].access_mode, 0);
			if (ret < 0) {
				pr_fail("%s: faccessat %3.3o on chmod mode %3.3o failed: %d (%s)\n",
					args->name,
					modes[i].access_mode, (unsigned int)modes[i].chmod_mode,
					errno, strerror(errno));
			}
#endif
			if (modes[i].access_mode != 0) {
				const mode_t chmod_mode = modes[i].chmod_mode ^ all_mask;
				const bool s_ixusr = chmod_mode & S_IXUSR;
				const bool dont_ignore = !(is_root && s_ixusr);

				ret = fchmod(fd, chmod_mode);
				if (CHMOD_ERR(ret)) {
					pr_err("%s: fchmod %3.3o failed: %d (%s)\n",
						args->name, (unsigned int)chmod_mode,
						errno, strerror(errno));
					goto tidy;
				}
				ret = access(filename, modes[i].access_mode);
				if ((ret == 0) && dont_ignore) {
					pr_fail("%s: access %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)\n",
						args->name,
						modes[i].access_mode, (unsigned int)chmod_mode,
						errno, strerror(errno));
				}
#if defined(HAVE_FACCESSAT)
				ret = faccessat(AT_FDCWD, filename, modes[i].access_mode, AT_SYMLINK_NOFOLLOW);
				if ((ret == 0) && dont_ignore) {
					pr_fail("%s: faccessat %3.3o on chmod mode %3.3o was ok (not expected): %d (%s)\n",
						args->name,
						modes[i].access_mode, (unsigned int)chmod_mode,
						errno, strerror(errno));
				}
#endif
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0) {
		(void)fchmod(fd, 0666);
		(void)close(fd);
	}
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

static const help_t help[] = {
	{ NULL,	"access N",	"start N workers that stress file access permissions" },
	{ NULL,	"access-ops N",	"stop after N file access bogo operations" },
	{ NULL, NULL,		NULL }
};

stressor_info_t stress_access_info = {
	.stressor = stress_access,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
