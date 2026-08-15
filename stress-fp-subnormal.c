/*
 * Copyright (C) 2026      Colin Ian King
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
#include "core-asm-generic.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"

#include <float.h>
#include <math.h>

#define LOOPS_PER_CALL	(65536)

#define FLT_TINY1	(1.4012984643248170709237295832899161312802619418765157717570682838897910826858606014866381883621215820e-45f)
#define FLT_TINY2	(4.2038953929744512127711887498697483938407858256295473152712048516693732480575818044599145650863647461e-45f)
#define FLT_ONEISH	(1.00000011920928955078125f)

#define DBL_TINY1	(4.9406564584124654417656879286822137236505980261432476442558568250067550727020875186529983636163599238e-324)
#define DBL_TINY2	(1.4821969375237396325297063786046641170951794078429742932767570475020265218106262555958995090849079771e-323)
#define DBL_ONEISH	(1.0000000000000002220446049250313080847263336181640625)

#define LDBL_TINY1 	(1.0935598595647423807585217800858259449197152447080690031162941461085115065900193210428682125877481616e-4950L)
#define LDBL_TINY2 	(3.6451995318824746025284059336194198163990508156935633437209804870283716886333977368095607086258272052e-4951L)
#define LDBL_ONEISH	(1.000000000000000000108420217248550443400745280086994171142578125L)

#define STRESS_FP_TYPE_LONG_DOUBLE	(0)
#define STRESS_FP_TYPE_DOUBLE		(1)
#define STRESS_FP_TYPE_FLOAT		(2)
#define STRESS_FP_TYPE_ALL		(4)

static const stress_help_t help[] = {
	{ NULL,	"fp-subnormal N",	 "start N workers performing subnormal floating point math ops" },
	{ NULL,	"fp-subnormal-method M", "select the subnormal floating point method to operate with" },
	{ NULL,	"fp-subnormal-ops N",	 "stop after N subnormal floating point math bogo operations" },
	{ NULL,	NULL,			 NULL }
};

typedef struct {
	struct {
		long double tiny1;	/* denormalized tiny value */
		long double tiny2;	/* denormalized tiny value */
		long double oneish;	/* almost 1 */
		long double r[2];	/* result of computation */
	} ld;
	struct {
		double tiny1;		/* denormalized tiny value */
		double tiny2;		/* denormalized tiny value */
		double oneish;		/* almost 1 */
		double r[2];		/* result of computation */
	} d;
	struct {
		float tiny1;		/* denormalized tiny value */
		float tiny2;		/* denormalized tiny value */
		float oneish;		/* almost 1 */
		float r[2];		/* result of computation */
	} f;
} fp_data_t;

typedef double (*stress_fp_subnormal_func_t)(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx);

static double stress_fp_subnormal_all(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx);

#define STRESS_FP_ADD(type, field, name, do_bogo_ops)		\
static double OPTIMIZE1 name(					\
	stress_args_t *args,					\
	fp_data_t *fp_data,					\
	const int idx)						\
{								\
	register int i;						\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1;						\
	double t2;						\
	const uint8_t rnd = stress_mwc1();			\
	const type v1 = rnd ? fp_data->field.tiny1 :		\
			fp_data->field.tiny2;			\
	const type v2 = rnd ? fp_data->field.tiny2 :		\
			fp_data->field.tiny1;			\
								\
	for (i = 0; i < loops; i++) 				\
		fp_data->field.r[idx] = 0.0;			\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops; i++) {				\
		register type tmp;				\
								\
		tmp = v1 + v2;					\
		stress_asm_mb();				\
		fp_data->field.r[idx] += tmp;			\
		stress_asm_mb();				\
								\
		tmp = v2 + v1;					\
		stress_asm_mb();				\
		fp_data->field.r[idx] += tmp;			\
		stress_asm_mb();				\
	}							\
	t2 = stress_time_now();					\
								\
	if (do_bogo_ops)					\
		stress_bogo_inc(args);				\
	return t2 - t1;						\
}

#define STRESS_FP_SUB(type, field, name, do_bogo_ops)		\
static double OPTIMIZE1 name(					\
	stress_args_t *args,					\
	fp_data_t *fp_data,					\
	const int idx)						\
{								\
	register int i;						\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1;						\
	double t2;						\
	const uint8_t rnd = stress_mwc1();			\
	const type v1 = rnd ? fp_data->field.tiny1 :		\
			      fp_data->field.tiny2;		\
	const type v2 = rnd ? fp_data->field.tiny2 :		\
			      fp_data->field.tiny1;		\
								\
	for (i = 0; i < loops; i++) 				\
		fp_data->field.r[idx] = (type)0.0;		\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops; i++) {				\
		register type tmp;				\
								\
		tmp = v2 - v1;					\
		stress_asm_mb();				\
		fp_data->field.r[idx] -= tmp;			\
		stress_asm_mb();				\
								\
		tmp = v1 - v2;					\
		stress_asm_mb();				\
		fp_data->field.r[idx] -= tmp;			\
		stress_asm_mb();				\
	}							\
	t2 = stress_time_now();					\
								\
	if (do_bogo_ops)					\
		stress_bogo_inc(args);				\
	return t2 - t1;						\
}

#define STRESS_FP_MUL(type, field, name, do_bogo_ops)		\
static double OPTIMIZE1 name(					\
	stress_args_t *args,					\
	fp_data_t *fp_data,					\
	const int idx)						\
{								\
	register int i;						\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1;						\
	double t2;						\
	const uint8_t rnd = stress_mwc1();			\
	const type v1 = rnd ? fp_data->field.tiny1 :		\
			fp_data->field.tiny2;			\
	const type v2 = rnd ? fp_data->field.tiny2 :		\
			fp_data->field.tiny1;			\
	const type oneish = fp_data->field.oneish;		\
								\
	for (i = 0; i < loops; i++) 				\
		fp_data->field.r[idx] = oneish;			\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops; i++) {				\
		register type tmp;				\
								\
		tmp = v2 * oneish;				\
		stress_asm_mb();				\
		fp_data->field.r[idx] += tmp;			\
		stress_asm_mb();				\
								\
		tmp = v1 * oneish;				\
		fp_data->field.r[idx] -= tmp;			\
		stress_asm_mb();				\
	}							\
	t2 = stress_time_now();					\
								\
	if (do_bogo_ops)					\
		stress_bogo_inc(args);				\
	return t2 - t1;						\
}

#define STRESS_FP_DIV(type, field, name, do_bogo_ops)		\
static double OPTIMIZE1 name(					\
	stress_args_t *args,					\
	fp_data_t *fp_data,					\
	const int idx)						\
{								\
	register int i;						\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1;						\
	double t2;						\
	const uint8_t rnd = stress_mwc1();			\
	const type v1 = rnd ? fp_data->field.tiny1 :		\
			fp_data->field.tiny2;			\
	const type v2 = rnd ? fp_data->field.tiny2 :		\
			fp_data->field.tiny1;			\
	const type oneish = fp_data->field.oneish;		\
								\
	for (i = 0; i < loops; i++) 				\
		fp_data->field.r[idx] = oneish;			\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops; i++) {				\
		register type tmp;				\
								\
		tmp = oneish / v1;				\
		stress_asm_mb();				\
		fp_data->field.r[idx] += tmp;			\
		stress_asm_mb();				\
								\
		tmp = oneish / v2;				\
		stress_asm_mb();				\
		fp_data->field.r[idx] -= tmp;			\
		stress_asm_mb();				\
	}							\
	t2 = stress_time_now();					\
								\
	if (do_bogo_ops)					\
		stress_bogo_inc(args);				\
	return t2 - t1;						\
}

STRESS_FP_ADD(long double, ld, stress_fp_subnormal_ldouble_add, true)
STRESS_FP_SUB(long double, ld, stress_fp_subnormal_ldouble_sub, true)
STRESS_FP_MUL(long double, ld, stress_fp_subnormal_ldouble_mul, true)
STRESS_FP_DIV(long double, ld, stress_fp_subnormal_ldouble_div, true)

STRESS_FP_ADD(double, d, stress_fp_subnormal_double_add, true)
STRESS_FP_SUB(double, d, stress_fp_subnormal_double_sub, true)
STRESS_FP_MUL(double, d, stress_fp_subnormal_double_mul, true)
STRESS_FP_DIV(double, d, stress_fp_subnormal_double_div, true)

STRESS_FP_ADD(float, f, stress_fp_subnormal_float_add, true)
STRESS_FP_SUB(float, f, stress_fp_subnormal_float_sub, true)
STRESS_FP_MUL(float, f, stress_fp_subnormal_float_mul, true)
STRESS_FP_DIV(float, f, stress_fp_subnormal_float_div, true)

typedef struct {
	const char *name;
	const char *description;
	const stress_fp_subnormal_func_t	fp_func;
	const int fp_type;
} stress_fp_subnormal_funcs_t;

static const stress_fp_subnormal_funcs_t stress_fp_subnormal_funcs[] = {
	{ "all",		"all fp methods",	stress_fp_subnormal_all,	STRESS_FP_TYPE_ALL },

	{ "floatadd",		"float add",		stress_fp_subnormal_float_add,	STRESS_FP_TYPE_FLOAT },
	{ "doubleadd",		"double add",		stress_fp_subnormal_double_add,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoubleadd",		"long double add",	stress_fp_subnormal_ldouble_add,STRESS_FP_TYPE_LONG_DOUBLE },

	{ "floatsub",		"float subtract",	stress_fp_subnormal_float_sub,	STRESS_FP_TYPE_FLOAT },
	{ "doublesub",		"double subtract",	stress_fp_subnormal_double_sub,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublesub",		"long double subtract",	stress_fp_subnormal_ldouble_sub,STRESS_FP_TYPE_LONG_DOUBLE },

	{ "floatmul",		"float multiply",	stress_fp_subnormal_float_mul,	STRESS_FP_TYPE_FLOAT },
	{ "doublemul",		"double multiply",	stress_fp_subnormal_double_mul,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublemul",		"long double multiply",	stress_fp_subnormal_ldouble_mul,STRESS_FP_TYPE_LONG_DOUBLE },

	{ "floatdiv",		"float divide",		stress_fp_subnormal_float_div,	STRESS_FP_TYPE_FLOAT },
	{ "doublediv",		"double divide",	stress_fp_subnormal_double_div,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublediv",		"long double divide",	stress_fp_subnormal_ldouble_div,STRESS_FP_TYPE_LONG_DOUBLE },
};

static stress_metrics_t stress_fp_subnormal_metrics[SIZEOF_ARRAY(stress_fp_subnormal_funcs)];

#define STRESS_NUM_FP_FUNCS	(SIZEOF_ARRAY(stress_fp_subnormal_funcs))

typedef struct {
	const int fp_type;
	const char *fp_description;
} fp_type_map_t;

static const fp_type_map_t fp_type_map[] = {
	{ STRESS_FP_TYPE_LONG_DOUBLE,	"long double" },
	{ STRESS_FP_TYPE_DOUBLE,	"double" },
	{ STRESS_FP_TYPE_FLOAT,		"float" },
	{ STRESS_FP_TYPE_ALL,		"all" },
};

static const char * PURE stress_fp_subnormal_type(const int fp_type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(fp_type_map); i++) {
		if (fp_type == fp_type_map[i].fp_type)
			return fp_type_map[i].fp_description;
	}
	return "unknown";
}

static int stress_fp_subnormal_call_method(
	stress_args_t *args,
	fp_data_t *fp_data,
	const size_t method,
	const bool verify)
{
	double dt;
	const stress_fp_subnormal_funcs_t *func = &stress_fp_subnormal_funcs[method];
	stress_metrics_t *metrics = &stress_fp_subnormal_metrics[method];

	dt = func->fp_func(args, fp_data, 0);
	metrics->duration += dt;
	metrics->count += LOOPS_PER_CALL;

	if ((method > 0) && (method < STRESS_NUM_FP_FUNCS && verify)) {
		const int fp_type = stress_fp_subnormal_funcs[method].fp_type;
		const char *method_name = stress_fp_subnormal_funcs[method].name;
		const char *fp_description = stress_fp_subnormal_type(fp_type);
		long double r0;
		long double r1;
		int ret;

		dt = func->fp_func(args, fp_data, 1);
		if (dt < 0.0)
			return EXIT_FAILURE;
		metrics->duration += dt;
		metrics->count += LOOPS_PER_CALL;

		/*
		 *  a SIGALRM during 2nd computation pre-verification can
		 *  cause long doubles on some arches to abort early, so
		 *  don't verify these results
		 */
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;

		switch (fp_type) {
		case STRESS_FP_TYPE_LONG_DOUBLE:
			ret = shim_memcmp(&fp_data->ld.r[0], &fp_data->ld.r[1], sizeof(fp_data->ld.r[0]));
			r0 = (long double)fp_data->ld.r[0];
			r1 = (long double)fp_data->ld.r[1];
			break;
		case STRESS_FP_TYPE_DOUBLE:
			ret = shim_memcmp(&fp_data->d.r[0], &fp_data->d.r[1], sizeof(fp_data->d.r[0]));
			r0 = (long double)fp_data->d.r[0];
			r1 = (long double)fp_data->d.r[1];
			break;
		case STRESS_FP_TYPE_FLOAT:
			ret = shim_memcmp(&fp_data->f.r[0], &fp_data->f.r[1], sizeof(fp_data->f.r[0]));
			r0 = (long double)fp_data->f.r[0];
			r1 = (long double)fp_data->f.r[1];
			break;
		default:
			/* Should never happen! */
			return EXIT_SUCCESS;
		}
		if (ret) {
			pr_fail("%s %s %s verification failure, got %Lf, expected %Lf\n",
				args->name, fp_description, method_name, r0, r1);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static double stress_fp_subnormal_all(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx)
{
	size_t i;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	(void)idx;

	for (i = 1; i < STRESS_NUM_FP_FUNCS; i++) {
		if (stress_fp_subnormal_call_method(args, fp_data, i, verify) == EXIT_FAILURE)
			return -1.0;
	}
	return 0.0;
}

static int stress_fp(stress_args_t *args)
{
	size_t i;
	size_t mmap_size;
	fp_data_t *fp_data;
	size_t fp_subnormal_method = 0;	/* "all" */
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	stress_signal_catch_sigill();

	mmap_size = sizeof(*fp_data);
	fp_data = (fp_data_t *)stress_mmap_populate(NULL, mmap_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (fp_data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap floating point elements%s, skipping stressor\n",
			args->name, stress_memory_free_get());
		return EXIT_NO_RESOURCE;
	}
	stress_memory_anon_name_set(fp_data, mmap_size, "fp-data");
	(void)stress_madvise_mergeable(fp_data, mmap_size);

	(void)stress_setting_get("fp-subnormal-method", &fp_subnormal_method);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	for (i = 0; i < SIZEOF_ARRAY(stress_fp_subnormal_metrics); i++) {
		stress_fp_subnormal_metrics[i].duration = 0.0;
		stress_fp_subnormal_metrics[i].count = 0.0;
	}

#if ((DBL_MIN_EXP == LDBL_MIN_EXP) ||	\
     defined(STRESS_ARCH_PPC64) ||	\
     defined(STRESS_ARCH_PPC))
	fp_data->ld.tiny1 = DBL_TINY1;
	fp_data->ld.tiny2 = DBL_TINY2;
	fp_data->ld.oneish = DBL_ONEISH;
	fp_data->ld.r[0] = DBL_TINY1;
	fp_data->ld.r[1] = DBL_TINY1;
#else
	fp_data->ld.tiny1 = LDBL_TINY1;
	fp_data->ld.tiny2 = LDBL_TINY2;
	fp_data->ld.oneish = LDBL_ONEISH;
	fp_data->ld.r[0] = LDBL_TINY1;
	fp_data->ld.r[1] = LDBL_TINY1;
#endif

	fp_data->d.tiny1 = DBL_TINY1;
	fp_data->d.tiny2 = DBL_TINY2;
	fp_data->d.oneish = DBL_ONEISH;
	fp_data->d.r[0] = DBL_TINY1;
	fp_data->d.r[1] = DBL_TINY1;

	fp_data->f.tiny1 = FLT_TINY1;
	fp_data->f.tiny2 = FLT_TINY2;
	fp_data->f.oneish = FLT_ONEISH;
	fp_data->f.r[0] = FLT_TINY1;
	fp_data->f.r[1] = FLT_TINY1;

	do {
		if (stress_fp_subnormal_call_method(args, fp_data, fp_subnormal_method, verify) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	for (i = 1; i < STRESS_NUM_FP_FUNCS; i++) {
		const double count = stress_fp_subnormal_metrics[i].count;
		const double duration = stress_fp_subnormal_metrics[i].duration;

		if ((duration > 0.0) && (count > 0.0)) {
			char msg[64];
			const double rate = count / duration;

			(void)snprintf(msg, sizeof(msg), "Mfp-ops per sec, %-20s", stress_fp_subnormal_funcs[i].description);
			stress_metrics_set(args, msg,
				rate / 1000000.0, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)fp_data, mmap_size);

	return rc;
}

static const char *stress_fp_subnormal_method(const size_t i)
{
	return (i < STRESS_NUM_FP_FUNCS) ? stress_fp_subnormal_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_fp_subnormal_method, "fp-subnormal-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_fp_subnormal_method },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("bogo-ops-stable"),
	STRESS_EX_FEATURE("cpu-instructions"),
	STRESS_EX_FEATURE("fp"),
	STRESS_EX_FEATURE("fp-division"),
	STRESS_EX_FEATURE("frontend-bound-bandwidth"),
	STRESS_EX_FEATURE("memory-loads"),
	STRESS_EX_FEATURE("user-time"),

	STRESS_EX_END,
};

const stressor_info_t stress_fp_subnormal_info = {
	.stressor = stress_fp,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.max_metrics_items = SIZEOF_ARRAY(stress_fp_subnormal_funcs),
	.exercises = exercises,
};
