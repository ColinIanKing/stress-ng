// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

/*
 *  stress_set_judy_size()
 *      set judy size from given option string
 */
static int stress_set_judy_size(const char *opt)
{
	uint64_t judy_size;

	judy_size = stress_get_uint64(opt);
	stress_check_range("judy-size", judy_size,
		MIN_JUDY_SIZE, MAX_JUDY_SIZE);
	return stress_set_setting("judy-size", TYPE_ID_UINT64, &judy_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_judy_size,	stress_set_judy_size },
	{ 0,			NULL }
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
static int OPTIMIZE3 stress_judy(const stress_args_t *args)
{
	uint64_t judy_size = DEFAULT_JUDY_SIZE;
	size_t n;
	register Word_t i, j;
	double duration[JUDY_OP_MAX], count[JUDY_OP_MAX];
	size_t k;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

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

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		Pvoid_t PJLArray = (Pvoid_t)NULL;
		Word_t *pvalue;
		int rc;
		double t;

		/* Step #1, populate Judy array in sparse index order */
		t = stress_time_now();
		for (i = 0; i < n; i++) {
			Word_t idx = gen_index(i);

			JLI(pvalue, PJLArray, idx);
			if (UNLIKELY((pvalue == NULL) || (pvalue == PJERR))) {
				pr_err("%s: cannot allocate new "
					"judy node\n", args->name);
				for (j = 0; j < n; j++) {
					JLD(rc, PJLArray, idx);
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
			Word_t idx = gen_index(i);

			JLG(pvalue, PJLArray, idx);
			if (UNLIKELY(verify)) {
				if (UNLIKELY(!pvalue)) {
					pr_fail("%s: element %" PRIu32
						"could not be found\n",
						args->name, (uint32_t)idx);
				} else {
					if (UNLIKELY((uint32_t)*pvalue != i))
						pr_fail("%s: element "
							"%" PRIu32 " found %" PRIu32
							", expecting %" PRIu32 "\n",
							args->name, (uint32_t)idx,
							(uint32_t)*pvalue, (uint32_t)i);
				}
			}
		}
		duration[JUDY_OP_FIND] += stress_time_now() - t;
		count[JUDY_OP_FIND] += n;

		/* Step #3, delete, reverse index order */
		t = stress_time_now();
		for (j = n -1, i = 0; i < n; i++, j--) {
			Word_t idx = gen_index(j);

			JLD(rc, PJLArray, idx);
			if (UNLIKELY(verify && (rc != 1)))
				pr_fail("%s: element %" PRIu32 " could not "
					"be found\n", args->name, (uint32_t)idx);
		}
		duration[JUDY_OP_DELETE] += stress_time_now() - t;
		count[JUDY_OP_DELETE] += n;

		stress_bogo_inc(args);
	} while (stress_continue(args));

abort:
	for (k = 0; k < JUDY_OP_MAX; k++) {
		char msg[64];
		const double rate = (duration[k] > 0.0) ? count[k] / duration[k] : 0.0;

		(void)snprintf(msg, sizeof(msg), "Judy %s operations per sec", judy_ops[k]);
		stress_metrics_set(args, k, msg, rate);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_judy_info = {
	.stressor = stress_judy,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_judy_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without Judy.h or Judy library support"
};
#endif
