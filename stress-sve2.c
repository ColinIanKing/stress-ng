/*
 * Copyright (C) 2026
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
#include "core-arch.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-sched.h"

#if defined(STRESS_ARCH_ARM) &&		\
    defined(__ARM_FEATURE_SVE2)

#include <arm_sve.h>
#include <math.h>
#include <sys/auxv.h>

#define SVE2_ELEMENTS		(256)	/* per-vector-lane workload chunk */

typedef struct {
	/*  Inputs filled once, outputs of the SVE2 computation  */
	double doubles[SVE2_ELEMENTS];
	uint64_t u64s[SVE2_ELEMENTS];
	/*  Golden (scalar reference) results  */
	double golden_doubles[SVE2_ELEMENTS];
	uint64_t golden_u64s[SVE2_ELEMENTS];
} stress_sve2_data_t;

static const stress_help_t help[] = {
	{ NULL,	"sve2 N",	"start N workers exercising SVE2 vector datapaths" },
	{ NULL,	"sve2-ops N",	"stop after N SVE2 bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  sve2_supported()
 *	SVE2 instructions require both build-time SVE2 code generation
 *	and run-time SVE2 hardware.  The build-time part is implied by
 *	this code being compiled at all (guarded by __ARM_FEATURE_SVE2),
 *	the run-time part is checked via getauxval(AT_HWCAP).
 */
static int sve2_supported(const char *name)
{
	const unsigned long hwcap = getauxval(AT_HWCAP);

	if (!(hwcap & HWCAP_SVE)) {
		pr_inf_skip("%s: stressor will be skipped, "
			"CPU does not support SVE\n", name);
		return -1;
	}
	return 0;
}

/*
 *  sve2_fmla()
 *	svmla (fused multiply-add) on doubles: z = z + a * b
 *	with predicated lanes; result compared against a scalar
 *	fma() golden reference.
 */
static void sve2_fmla(stress_sve2_data_t *data)
{
	svfloat64_t zd = svld1_f64(svptrue_b64(), data->doubles);
	const svfloat64_t za = svdup_f64(1.0000000001);
	const svfloat64_t zb = svdup_f64(0.9999999999);
	size_t i;

	for (i = 0; i < 64; i++)
		zd = svmla_f64_m(svptrue_b64(), za, zd, zb);

	svst1_f64(svptrue_b64(), data->doubles, zd);
}

static void golden_fmla(stress_sve2_data_t *data)
{
	const double a = 1.0000000001;
	const double b = 0.9999999999;
	size_t i, j;

	for (i = 0; i < SVE2_ELEMENTS; i++) {
		double d = data->doubles[i];

		for (j = 0; j < 64; j++)
			d = fma(a, d, b);
		data->golden_doubles[i] = d;
	}
}

/*
 *  sve2_bitperm()
 *	SVE2 BEXT (bit extract, EOR-based gather) via svbext_u64:
 *	each output bit is selected from the input by the
 *	corresponding bit of the permutation mask.  Purely
 *	integer datapath, compared against a scalar reference.
 *	Note svbext requires the sve2-bitperm extension, so this
 *	section is guarded separately.
 */
#if defined(__ARM_FEATURE_SVE2_BITPERM) || \
    defined(__ARM_FEATURE_SVE_BITPERM)
static void sve2_bitperm(stress_sve2_data_t *data)
{
	svuint64_t zd = svld1_u64(svptrue_b64(), data->u64s);
	const svuint64_t zm = svdup_u64(0xA5A5A5A55A5A5A5AULL);
	size_t i;

	for (i = 0; i < 64; i++)
		zd = svbext_u64(zd, zm);

	svst1_u64(svptrue_b64(), data->u64s, zd);
}

static void golden_bitperm(stress_sve2_data_t *data)
{
	const uint64_t mask = 0xA5A5A5A55A5A5A5AULL;
	size_t i, j;

	/*
	 *  BEXT (bit extract): the bits of v selected by the set
	 *  bits of the mask are compressed in order to the bottom
	 *  of the result:
	 *
	 *    result[0] = v[lowest set bit index of mask]
	 *    result[1] = v[next set bit index of mask], etc.
	 */
	for (i = 0; i < SVE2_ELEMENTS; i++) {
		uint64_t v = data->u64s[i];

		for (j = 0; j < 64; j++) {
			uint64_t r = 0;
			int out = 0;
			int b;

			for (b = 0; b < 64; b++) {
				if ((mask >> b) & 1ULL) {
					r |= ((v >> b) & 1ULL) << out;
					out++;
				}
			}
			v = r;
		}
		data->golden_u64s[i] = v;
	}
}
#endif

/*
 *  sve2_verify_fail()
 *	CORE179-style bit level diagnostics: first differing
 *	element, expected/actual bit patterns, flipped bit count.
 */
static void sve2_verify_fail(
	const char *name,
	const char *what,
	const size_t idx,
	const uint64_t expected,
	const uint64_t actual)
{
	const uint64_t xor = expected ^ actual;

	pr_fail("%s: %s mismatch at element %zu: "
		"expected 0x%16.16" PRIx64 ", actual 0x%16.16" PRIx64 ", "
		"%u bit(s) flipped (xor 0x%16.16" PRIx64 ")\n",
		name, what, idx, expected, actual,
		stress_bitops_popcount64(xor), xor);
}

static int stress_sve2(stress_args_t *args)
{
	stress_sve2_data_t *data;
	const size_t sz = sizeof(*data);
	uint32_t i;
	int rc = EXIT_SUCCESS;

	data = (stress_sve2_data_t *)stress_mmap_populate(NULL, sz,
		PROT_READ | PROT_WRITE,
#if defined(HAVE_MAP_ANONYMOUS)
		MAP_ANONYMOUS |
#endif
		MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes, skipping\n",
			args->name, sz);
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_mergeable(data, sz);

	/*  Seed the input data  */
	for (i = 0; i < SVE2_ELEMENTS; i++) {
		data->doubles[i] = (double)(i + 1) * 1.000001;
		data->u64s[i] = stress_mwc64();
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		sve2_fmla(data);
		golden_fmla(data);
		for (i = 0; i < SVE2_ELEMENTS; i++) {
			uint64_t b1, b2;

			if (data->doubles[i] != data->golden_doubles[i]) {
				(void)shim_memcpy(&b1, &data->golden_doubles[i], sizeof(b1));
				(void)shim_memcpy(&b2, &data->doubles[i], sizeof(b2));
				sve2_verify_fail(args->name, "sve2 fmla result", i, b1, b2);
				rc = EXIT_FAILURE;
			}
		}

#if defined(__ARM_FEATURE_SVE2_BITPERM) || \
    defined(__ARM_FEATURE_SVE_BITPERM)
		sve2_bitperm(data);
		golden_bitperm(data);
		for (i = 0; i < SVE2_ELEMENTS; i++) {
			if (data->u64s[i] != data->golden_u64s[i]) {
				sve2_verify_fail(args->name, "sve2 bitperm result", i,
					data->golden_u64s[i], data->u64s[i]);
				rc = EXIT_FAILURE;
			}
		}
#endif

		/*  re-seed for next round; keep values changing  */
		for (i = 0; i < SVE2_ELEMENTS; i++) {
			data->doubles[i] = (double)(i + 1) * 1.000001 + (double)stress_mwc16();
			data->u64s[i] = stress_mwc64();
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)munmap(data, sz);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("sve2-fmla"),
	STRESS_EX_FEATURE("sve2-bitperm"),

	STRESS_EX_END,
};

const stressor_info_t stress_sve2_info = {
	.stressor = stress_sve2,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_INTEGER,
	.verify = VERIFY_ALWAYS,
	.supported = sve2_supported,
	.help = help,
	.exercises = exercises,
};

#else

const stressor_info_t stress_sve2_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_INTEGER,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "built for non-aarch64 target or compiler without SVE2 support"
};

#endif
