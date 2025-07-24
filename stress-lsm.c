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
#include "core-mmap.h"

#if defined(HAVE_LINUX_LSM_H)
#include <linux/lsm.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"lsm N",	"start N workers that exercise lsm kernel system calls" },
	{ NULL,	"lsm-ops N",	"stop after N lsm bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__NR_lsm_list_modules) &&	\
    defined(__NR_lsm_get_self_attr) &&	\
    defined(__NR_lsm_set_self_attr) &&	\
    defined(HAVE_LINUX_LSM_H)

/*
 *  shim_lsm_list_modules()
 *	shim system call wrapper for lsm_list_modules()
 */
static int shim_lsm_list_modules(uint64_t *ids, size_t *size, uint32_t flags)
{
	return (int)syscall(__NR_lsm_list_modules, ids, &size, flags);
}

/*
 *  shim_lsm_get_self_attr
 *	shim system call wrapper for lsm_get_self_attr()
 */
static int shim_lsm_get_self_attr(unsigned int attr, struct lsm_ctx *ctx, size_t *size, uint32_t flags)
{
	return (int)syscall(__NR_lsm_get_self_attr, attr, ctx, size, flags);
}

/*
 *  shim_lsm_set_self_attr
 *	shim system call wrapper for lsm_set_self_attr()
 */
static int shim_lsm_set_self_attr(unsigned int attr, struct lsm_ctx *ctx, size_t size, uint32_t flags)
{
	return (int)syscall(__NR_lsm_set_self_attr, attr, ctx, size, flags);
}

/*
 *  stress_lsm()
 *	stress lsm
 */
static int stress_lsm(stress_args_t *args)
{
	void *buf;
	int rc = EXIT_SUCCESS;
	const size_t buf_size = args->page_size * 8;
	bool lsm_id_undef = false, lsm_id_reserved = false, lsm_id_defined = false;
	double list_duration = 0.0, list_count = 0.0;
	double get_duration = 0.0, get_count = 0.0;
	double rate;

	static const unsigned int attr[] = {
#if defined(LSM_ATTR_CURRENT)
		LSM_ATTR_CURRENT,
#endif
#if defined(LSM_ATTR_EXEC)
		LSM_ATTR_EXEC,
#endif
#if defined(LSM_ATTR_FSCREATE)
		LSM_ATTR_FSCREATE,
#endif
#if defined(LSM_ATTR_KEYCREATE)
		LSM_ATTR_KEYCREATE,
#endif
#if defined(LSM_ATTR_PREV)
		LSM_ATTR_PREV,
#endif
#if defined(LSM_ATTR_SOCKCREATE)
		LSM_ATTR_SOCKCREATE,
#endif
		0,
	};

	buf = stress_mmap_populate(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu byte sized buffer%s, errno=%d (%s),"
			"skipping stressor\n",
			args->name, buf_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buf, buf_size, "lsm-data");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t size, j;
		int i, ret;
		uint64_t *ids = (uint64_t *)buf;
		double t;

		size = buf_size;
		t = stress_time_now();
		ret = shim_lsm_list_modules(ids, &size, 0);
		if (LIKELY(ret >= 0)) {
			list_duration += stress_time_now() - t;
			list_count += 1.0;
		} else {
			if (errno == ENOSYS) {
				pr_inf_skip("%s: lsm_list_modules system call is not supported, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				goto err;
			} else {
				pr_inf("%s: lsm_list_modules failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		}

		/* exercise invalid flags */
		size = 1;
		ret = shim_lsm_list_modules(ids, &size, ~0);
		if (UNLIKELY((ret >= 0) || ((ret < 0) && (errno != EINVAL)))) {
			pr_fail("%s: lsm_list_modules call with invalid flags should return -1, got %d, errno=%d (%s) instead\n",
				args->name, ret, errno, strerror(errno));
			goto err;
		}

		/* exercise NULL ids */
		size = 1;
		ret = shim_lsm_list_modules(NULL, &size, 0);
		if (UNLIKELY((ret >= 0) || ((ret < 0) && (errno != EFAULT)))) {
			pr_fail("%s: lsm_list_modules call with NULL ids should return -1, got %d, errno=%d (%s) instead\n",
				args->name, ret, errno, strerror(errno));
			goto err;
		}

		for (j = 0; j < SIZEOF_ARRAY(attr); j++) {
			struct lsm_ctx *ctx = (struct lsm_ctx *)buf;
			struct lsm_ctx *ctx_end = (struct lsm_ctx *)((uintptr_t)buf + buf_size);
			struct lsm_ctx tmp_ctx ALIGNED(8);

			size = buf_size;
			t = stress_time_now();

			ret = shim_lsm_get_self_attr(attr[j], ctx, &size, 0);
			if (ret < 0)
				continue;
			get_duration += stress_time_now() - t;
			get_count += 1.0;

			for (i = 0; i < ret; i++) {
				if (ctx->id == LSM_ID_UNDEF)
					lsm_id_undef = true;
				else if ((ctx->id >= 1) && (ctx->id < 100))
					lsm_id_reserved = true;
				else if (ctx->id >= LSM_ID_CAPABILITY)
					lsm_id_defined = true;
				ctx = (struct lsm_ctx *)((uint8_t *)ctx + sizeof(*ctx) + ctx->ctx_len);
				if (ctx >= ctx_end)
					break;
			}

			/* exercise invalid attr */
			size = buf_size;
			ret = shim_lsm_get_self_attr(~0, ctx, &size, 0);
			if (UNLIKELY((ret >= 0) || ((ret < 0) && (errno != EOPNOTSUPP)))) {
				pr_fail("%s: lsm_get_self_attr call with invalid attr should return -1, got %d, errno=%d (%s) instead\n",
					args->name, ret, errno, strerror(errno));
				goto err;
			}

			/* exercise invalid ctx */
			size = buf_size;
			ret = shim_lsm_get_self_attr(attr[j], (struct lsm_ctx *)~(uintptr_t)0, &size, 0);
			if (UNLIKELY((ret >= 0) || ((ret < 0) && (errno != EFAULT)))) {
				pr_fail("%s: lsm_get_self_attr call with NULL ctx should return -1, got %d, errno=%d (%s) instead\n",
					args->name, ret, errno, strerror(errno));
				goto err;
			}

			/* exercise invalid flags */
			size = buf_size;
			ret = shim_lsm_get_self_attr(attr[j], ctx, &size, ~0);
			if (UNLIKELY((ret >= 0) || ((ret < 0) && (errno != EINVAL)))) {
				pr_fail("%s: lsm_get_self_attr call with invalid flags should return -1, got %d, errno=%d (%s) instead\n",
					args->name, ret, errno, strerror(errno));
				goto err;
			}

			/*
			 *  exercise invalid ctx_len, see Linux commits
			 *  a04a1198088a and d8bdd795d383
			 */
			shim_memcpy(&tmp_ctx, ctx, sizeof(tmp_ctx));

			tmp_ctx.id = LSM_ID_APPARMOR;
			tmp_ctx.flags = 0;
			tmp_ctx.len = sizeof(tmp_ctx);
			tmp_ctx.ctx_len = -sizeof(tmp_ctx);
			VOID_RET(int, shim_lsm_set_self_attr(LSM_ATTR_CURRENT, &tmp_ctx, sizeof(tmp_ctx), 0));
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (list_duration > 0.0) ? list_count / list_duration  : 0.0;
	stress_metrics_set(args, 0, "lsm_list_modules calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (get_duration > 0.0) ? get_count / get_duration  : 0.0;
	stress_metrics_set(args, 1, "lsm_get_self_attr calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

err:
	pr_dbg("%s: got LSM IDs: undefined: %s, reserved: %s, defined: %s\n",
		args->name,
		lsm_id_undef ? "yes" : "no",
		lsm_id_reserved ? "yes" : "no",
		lsm_id_defined ? "yes" : "no");

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap(buf, buf_size);

	return rc;
}

const stressor_info_t stress_lsm_info = {
	.stressor = stress_lsm,
	.classifier = CLASS_OS | CLASS_SECURITY,
	.help = help
};
#else
const stressor_info_t stress_lsm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_SECURITY,
	.help = help,
	.unimplemented_reason = "built without linux/lsm.h or lsm_list_modules or lsm_get_self_attr system calls"
};
#endif
