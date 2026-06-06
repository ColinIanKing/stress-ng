/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-killpid.h"

#include <sys/stat.h>

#if defined(HAVE_SYS_ACCT_H)
#include <sys/acct.h>
#endif

#if !defined(ACCT_VERSION)
#define ACCT_VERSION	(3)
#endif

#define MAX_ACCT_FILESIZE	((off_t)(16 * MB))

static const stress_help_t help[] = {
	{ NULL,	"acct N",		"start N workers exercising process accouting" },
	{ NULL,	"acct-ops N",		"stop after reading N process exiting accounting records" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_acct_supported()
 *      check if acct is supported
 */
static int stress_acct_supported(const char *name)
{
	if (!stress_capabilities_check(SHIM_CAP_SYS_PACCT)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_PACCT "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(HAVE_ACCT) &&	\
    defined(HAVE_SYS_ACCT_H) &&	\
    defined(HAVE_ACCT_V3) && 	\
    defined(__linux__)
/*
 *  stress_acct
 *	stress accounting acct() system call
 */
static int stress_acct(stress_args_t *args)
{
	int fd = -1;
	int ret;
	int rc = EXIT_SUCCESS;
	char filename[PATH_MAX];
	bool version_warn = false;

	ret = stress_fs_temp_dir_make_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_fs_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_temp;
	}
	stress_fs_file_rw_hint_short(fd);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		struct stat statbuf;

		if (fstat(fd, &statbuf) == 0) {
			if (statbuf.st_size > MAX_ACCT_FILESIZE) {
				VOID_RET(int, shim_fsync(fd));
				VOID_RET(int, ftruncate(fd, 0));
			}
		}

		ret = acct(filename);
		if (ret < 0) {
			pr_inf("%s: acct(%s) failed, errno=%d (%s)\n", args->name, filename, errno, strerror(errno));
			break;
		}

		pid = fork();
		if (pid < 0) {
			continue;
		} else if (pid == 0) {
			_exit(0);
		} else {
			int status;

			(void)shim_kill(pid, SIGKILL);

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0)
				(void)stress_kill_pid_wait(pid, &status);
		}

		do {
			ssize_t sz;
			struct acct_v3 data;

			(void)memset(&data, 0, sizeof(data));

			sz = read(fd, &data, sizeof(data));
			if (sz < (ssize_t)sizeof(data))
				break;
			if (data.ac_version != ACCT_VERSION) {
				if (!version_warn) {
					pr_warn("%s: expected accounting version %d, got %d instead\n",
						args->name, ACCT_VERSION, (int)data.ac_version);
					version_warn = true;
				}
				break;
			}
			if (data.ac_flag & AXSIG)
				stress_bogo_inc(args);
		} while (stress_continue(args));

		ret = acct(NULL);
		if (ret < 0) {
			pr_inf("%s: acct(NULL) failed, errno=%d (%s)\n", args->name, errno, strerror(errno));
			break;
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (fd != -1)
		(void)close(fd);
	(void)shim_unlink(filename);
tidy_temp:
	(void)stress_fs_temp_dir_rm_args(args);
	return rc;
}

const stressor_info_t stress_acct_info = {
	.stressor = stress_acct,
	.classifier = CLASS_OS,
	.verify = VERIFY_NONE,
	.supported = stress_acct_supported,
	.help = help
};
#else
const stressor_info_t stress_acct_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help,
	.supported = stress_acct_supported,
	.unimplemented_reason = "built without acct() or sys/acct.h or struct acct_v3  support"
};
#endif
