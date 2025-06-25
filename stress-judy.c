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

#if defined(HAVE_JUDY_H)
#include <Judy.h>
#endif

#define MIN_JUDY_SIZE		(1 * KB)
#define MAX_JUDY_SIZE		(4 * MB)
#define DEFAULT_JUDY_SIZE	(256 * KB)

#define JUDY_OP_INSERT		(0)
#define JUDY_OP_FIND		(1)
#define JUDY_OP_DELETE		(2)
#define JUDY_OP_MAX		(3)

static const stress_help_t help[] = {
	{ NULL,	"judy N",	"start N workers that exercise a judy array search" },
	{ NULL,	"judy-ops N",	"stop after N judy array search bogo operations" },
	{ NULL,	"judy-size N",	"number of 32 bit integers to insert into judy array" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_judy_size, "judy-size", TYPE_ID_UINT64, MIN_JUDY_SIZE, MAX_JUDY_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_JUDY_H) && \
    defined(HAVE_LIB_JUDY)
/*
 *  generate a unique large index position into a Judy array
 *  from a known small index
 */
static inline OPTIMIZE3 Word_t gen_index(const Word_t idx)
{
	return ((~idx & 0xff) << 24) | (idx & 0x00ffffff);
}

/*
 *  stress_judy()
 *	stress a judy array, exercises cache/memory
 */
static int OPTIMIZE3 stress_judy(stress_args_t *args)
{
	uint64_t judy_size = DEFAULT_JUDY_SIZE;
	size_t n;
	register Word_t i, j;
	double duration[JUDY_OP_MAX], count[JUDY_OP_MAX];
	size_t k;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_FAILURE;

	static const char * const judy_ops[] = {
		"insert",
		"find",
		"delete",
	};

	if (!stress_get_setting("judy-size", &judy_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			judy_size = MAX_JUDY_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			judy_size = MIN_JUDY_SIZE;
	}
	n = (size_t)judy_size;

	for (k = 0; k < JUDY_OP_MAX; k++) {
		duration[k] = 0.0;
		count[k] = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		Pvoid_t PJLArray = (Pvoid_t)NULL;
		Word_t *pvalue;
		int judyrc;
		double t;

		/* Step #1, populate Judy array in sparse index order */
		t = stress_time_now();
		for (i = 0; i < n; i++) {
			const Word_t idx = gen_index(i);

			JLI(pvalue, PJLArray, idx);
			if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR))) {
				pr_err("%s: cannot allocate new "
					"judy node%s\n", args->name,
					stress_get_memfree_str());
				for (j = 0; j < n; j++) {
					JLD(judyrc, PJLArray, idx);
				}
				goto abort;
			}
			*pvalue = i;
		}
		duration[JUDY_OP_INSERT] += stress_time_now() - t;
		count[JUDY_OP_INSERT] += n;

		/* Step #2, find */
		t = stress_time_now();
		for (i = 0; stress_continue_flag() && (i < n); i++) {
			const Word_t idx = gen_index(i);

			JLG(pvalue, PJLArray, idx);
			if (UNLIKELY(verify)) {
				if (UNLIKELY(!pvalue)) {
					pr_fail("%s: element %" PRIu32
						"could not be found\n",
						args->name, (uint32_t)idx);
					goto abort;
				} else {
					if (UNLIKELY((uint32_t)*pvalue != i)) {
						pr_fail("%s: element "
							"%" PRIu32 " found %" PRIu32
							", expecting %" PRIu32 "\n",
							args->name, (uint32_t)idx,
							(uint32_t)*pvalue, (uint32_t)i);
						goto abort;
					}
				}
			}
		}
		duration[JUDY_OP_FIND] += stress_time_now() - t;
		count[JUDY_OP_FIND] += n;

		/* Step #3, delete, reverse index order */
		t = stress_time_now();
		for (j = n -1, i = 0; i < n; i++, j--) {
			const Word_t idx = gen_index(j);

			JLD(judyrc, PJLArray, idx);
			if (UNLIKELY(verify && (judyrc != 1))) {
				pr_fail("%s: element %" PRIu32 " could not "
					"be found\n", args->name, (uint32_t)idx);
				goto abort;
			}
		}
		duration[JUDY_OP_DELETE] += stress_time_now() - t;
		count[JUDY_OP_DELETE] += n;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
abort:
	for (k = 0; k < JUDY_OP_MAX; k++) {
		char msg[64];
		const double rate = (duration[k] > 0.0) ? count[k] / duration[k] : 0.0;

		(void)snprintf(msg, sizeof(msg), "Judy %s operations per sec", judy_ops[k]);
		stress_metrics_set(args, k, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_judy_info = {
	.stressor = stress_judy,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_judy_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without Judy.h or Judy library support"
};
#endif
