/*
 * Copyright (C) 2024-2025 Colin Ian King.
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

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"statmount N",	   "start N workers exercising statmount and listmount" },
	{ NULL,	"statmount-ops N", "stop after N bogo statmount and listmount operations" },
	{ NULL,	NULL,		   NULL }
};

#if defined(__linux__) &&		\
    defined(__NR_statmount) &&		\
    defined(__NR_listmount) &&		\
    defined(__NR_statx) &&		\
    defined(MNT_ID_REQ_SIZE_VER0) &&	\
    defined(STATMOUNT_MNT_BASIC) &&	\
    defined(STATMOUNT_SB_BASIC) &&	\
    defined(STATX_MNT_ID_UNIQUE) &&	\
    defined(LSMT_ROOT)

/*
 *  shim_statmount()
 *	shim wrapper for Linux statmount system call
 */
static int shim_statmount(
	uint64_t mnt_id,
	uint64_t mask,
	struct statmount *buf,
	size_t bufsize,
	unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = mask,
	};

	return syscall(__NR_statmount, &req, buf, bufsize, flags);
}

/*
 *  shim_listmount()
 *	shim wrapper for Linux listmount system call
 */
static ssize_t shim_listmount(
	uint64_t mnt_id,
	uint64_t last_mnt_id,
	uint64_t list[],
	size_t num,
	unsigned int flags)
{
	struct mnt_id_req req = {
		.size = MNT_ID_REQ_SIZE_VER0,
		.mnt_id = mnt_id,
		.param = last_mnt_id,
	};

        return syscall(__NR_listmount, &req, list, num, flags);
}

/*
 *  stress_statmount_statroot()
 *	exercise stat'ing of root mount
 */
static int stress_statmount_statroot(
	stress_args_t *args,
	const uint64_t id,
	double *duration,
	double *count)
{
	struct statmount sm;
	double t;

	(void)shim_memset(&sm, 0, sizeof(sm));
	t = stress_time_now();
	if (UNLIKELY(shim_statmount(id, STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0) < 0)) {
		pr_fail("%s: statmount failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	(*duration) += stress_time_now() - t;
	(*count) += 1.0;

	if (UNLIKELY(sm.size != sizeof(sm))) {
		pr_fail("%s: statmount.size is %zu, expected size %zu\n",
			args->name, (size_t)sm.size, sizeof(sm));
		return EXIT_FAILURE;
	}
	if (UNLIKELY(sm.mnt_id != id)) {
		pr_fail("%s: statmount.mnt_id is %" PRIu64 ", expected %" PRIu64 "\n",
			args->name, (uint64_t)sm.mnt_id, id);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_statmount_listroot()
 *	get a list of mounts on / and stat them
 */
static int stress_statmount_listroot(
	stress_args_t *args,
	double *duration,
	double *count,
	int *max_mounts)
{
	int i, ret;
	uint64_t list[1024];

	ret = shim_listmount(LSMT_ROOT, 0, list, SIZEOF_ARRAY(list), 0);
	if (UNLIKELY(ret < 0)) {
		if (errno == ENOSYS)
			return EXIT_NO_RESOURCE;
		pr_fail("%s: shim_listmount on root failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (ret > *max_mounts)
		*max_mounts = ret;
	for (i = 0; i < ret; i++) {
		struct statmount sm;
		double t;

		(void)shim_memset(&sm, 0, sizeof(sm));
		t = stress_time_now();
		if (LIKELY(shim_statmount(list[i], STATMOUNT_MNT_BASIC, &sm, sizeof(sm), 0) >= 0)) {
			(*duration) += stress_time_now() - t;
			(*count) += 1.0;
		}
		(void)shim_memset(&sm, 0, sizeof(sm));
		t = stress_time_now();
		if (LIKELY(shim_statmount(list[i], STATMOUNT_SB_BASIC, &sm, sizeof(sm), 0) >= 0)) {
			(*duration) += stress_time_now() - t;
			(*count) += 1.0;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_statmount(stress_args_t *args)
{
	uint64_t id;
	shim_statx_t sx;
	int ret, rc = EXIT_SUCCESS;
	int max_mounts = 0;

	double duration = 0.0, count = 0.0, rate;

	if (shim_statmount(0, 0, NULL, 0, 0) < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s: statmount not implemented on this system, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
	}

	ret = shim_statx(AT_FDCWD, "/", 0, STATX_MNT_ID_UNIQUE, &sx);
	if (UNLIKELY(ret < 0)) {
		pr_inf_skip("%s: statx on / failed, errno=%d (%s), skipping stressor",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	id = sx.stx_mnt_id;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(stress_statmount_statroot(args, id, &duration, &count) == EXIT_FAILURE)) {
			rc = EXIT_FAILURE;
			break;
		}
		if (UNLIKELY(stress_statmount_listroot(args, &duration, &count, &max_mounts) == EXIT_FAILURE)) {
			rc = EXIT_FAILURE;
			break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (stress_instance_zero(args))
		pr_inf("%s: %d mount points exercised by statmount\n", args->name, max_mounts);

	rate = (duration > 0.0) ? count / duration  : 0.0;
	stress_metrics_set(args, 0, "statmount calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_statmount_info = {
	.stressor = stress_statmount,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_statmount_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux statmount or listmount"
};
#endif
