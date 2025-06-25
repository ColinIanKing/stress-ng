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
#include "core-io-priority.h"

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(__NR_ioprio_get)
#define HAVE_IOPRIO_GET
#endif

#if defined(__NR_ioprio_set)
#define HAVE_IOPRIO_SET
#endif

static const stress_help_t help[] = {
	{ NULL,	"ioprio N",	"start N workers exercising set/get iopriority" },
	{ NULL,	"ioprio-ops N",	"stop after N io bogo iopriority operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_IOPRIO_GET) &&	\
    defined(HAVE_IOPRIO_SET) && \
    defined(HAVE_PWRITEV)

#define MAX_IOV		(4)
#define BUF_SIZE	(32)

/*
 *  stress set/get io priorities
 *	stress system by rapid io priority changes
 */
static int stress_ioprio(stress_args_t *args)
{
	const uid_t uid = getuid();
#if defined(HAVE_GETPGRP)
	const pid_t grp = getpgrp();
#endif
	int fd, rc = EXIT_FAILURE, ret;
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto cleanup_dir;
	}
	(void)shim_unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i;
		struct iovec iov[MAX_IOV];
		char buffer[MAX_IOV][BUF_SIZE];

		if (shim_ioprio_get(IOPRIO_WHO_PROCESS, args->pid) < 0) {
			if (errno != EINVAL) {
				pr_fail("%s: ioprio_get(OPRIO_WHO_PROCESS, %" PRIdMAX"), "
					"errno=%d (%s)\n",
					args->name, (intmax_t)args->pid, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		if (shim_ioprio_get(IOPRIO_WHO_PROCESS, 0) < 0) {
			if (errno != EINVAL) {
				pr_fail("%s: ioprio_get(OPRIO_WHO_PROCESS, 0), "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
#if defined(HAVE_GETPGRP)
		if (shim_ioprio_get(IOPRIO_WHO_PGRP, grp) < 0) {
			if (errno != EINVAL) {
				pr_fail("%s: ioprio_get(OPRIO_WHO_PGRP, %" PRIdMAX "), "
					"errno=%d (%s)\n",
					args->name, (intmax_t)grp, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
#else
		UNEXPECTED
#endif
		if (shim_ioprio_get(IOPRIO_WHO_PGRP, 0) < 0) {
			if (errno != EINVAL) {
				pr_fail("%s: ioprio_get(OPRIO_WHO_PGRP, 0), "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		/*
		 *  Exercise invalid ioprio_get arguments
		 */
		(void)shim_ioprio_get(~0, 0);
		(void)shim_ioprio_get(IOPRIO_WHO_PROCESS, ~0);
		(void)shim_ioprio_get(IOPRIO_WHO_PGRP, ~0);
		(void)shim_ioprio_get(IOPRIO_WHO_USER, ~0);

		if (shim_ioprio_get(IOPRIO_WHO_USER, (int)uid) < 0) {
			if (errno != EINVAL) {
				pr_fail("%s: ioprio_get(OPRIO_WHO_USR, %d), "
					"errno=%d (%s)\n",
					args->name, uid, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;

		for (i = 0; i < MAX_IOV; i++) {
			(void)shim_memset(buffer[i], stress_mwc8(), BUF_SIZE);
			iov[i].iov_base = buffer[i];
			iov[i].iov_len = BUF_SIZE;
		}

		if (pwritev(fd, iov, MAX_IOV, (off_t)512 * stress_mwc16()) < 0) {
			if (errno != ENOSPC) {
				pr_fail("%s: pwritev failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		(void)shim_fsync(fd);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise invalid ioprio_set arguments
		 */
		(void)shim_ioprio_set(~0, 0, ~0);
		(void)shim_ioprio_set(IOPRIO_WHO_PROCESS, ~0, 0);
		(void)shim_ioprio_set(IOPRIO_WHO_PGRP, ~0, 0);
		(void)shim_ioprio_set(IOPRIO_WHO_USER, ~0, 0);

		if (shim_ioprio_set(IOPRIO_WHO_PROCESS, args->pid,
			IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0) {
			if ((errno != EPERM) && (errno != EINVAL)) {
				pr_fail("%s: ioprio_set("
					"IOPRIO_WHO_PROCESS, %" PRIdMAX ", "
					"(IOPRIO_CLASS_IDLE, 0)), "
					"errno=%d (%s)\n",
					args->name, (intmax_t)args->pid,
					errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (pwritev(fd, iov, MAX_IOV, (off_t)512 * stress_mwc16()) < 0) {
			if (errno != ENOSPC) {
				pr_fail("%s: pwritev failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto cleanup_file;
			}
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		(void)shim_fsync(fd);
		if (UNLIKELY(!stress_continue(args)))
			break;

		for (i = 0; i < 8; i++) {
			if (shim_ioprio_set(IOPRIO_WHO_PROCESS, args->pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, i)) < 0) {
				if ((errno != EPERM) && (errno != EINVAL)) {
					pr_fail("%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %" PRIdMAX ", "
						"(IOPRIO_CLASS_BE, %d)), "
						"errno=%d (%s)\n",
						args->name, (intmax_t)args->pid,
						i, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			if (pwritev(fd, iov, MAX_IOV, (off_t)512 * stress_mwc16()) < 0) {
				if (errno != ENOSPC) {
					pr_fail("%s: pwritev failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			(void)shim_fsync(fd);
		}
		if (UNLIKELY(!stress_continue(args)))
			break;
		for (i = 0; i < 8; i++) {
			if (shim_ioprio_set(IOPRIO_WHO_PROCESS, args->pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, i)) < 0) {
				if ((errno != EPERM) && (errno != EINVAL)) {
					pr_fail("%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %" PRIdMAX ", "
						"(IOPRIO_CLASS_RT, %d)), "
						"errno=%d (%s)\n",
						args->name, (intmax_t)args->pid,
						i, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			if (pwritev(fd, iov, MAX_IOV, (off_t)512 * stress_mwc16()) < 0) {
				if (errno != ENOSPC) {
					pr_fail("%s: pwritev failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			(void)shim_fsync(fd);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;

cleanup_file:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
cleanup_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_ioprio_info = {
	.stressor = stress_ioprio,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_ioprio_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/uio.h, ioprio_get(), ioprio_set() or pwritev() support"
};
#endif
