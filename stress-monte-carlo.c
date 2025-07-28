/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#include "core-builtin.h"
#include "core-asm-x86.h"
#include "core-asm-ppc64.h"
#include "core-cpu.h"

#include <math.h>

#define MIN_MONTE_CARLO_SAMPLES	(1)
#define MAX_MONTE_CARLO_SAMPLES	(0xffffffffULL)

/* Don't use HAVE_ASM_X86_RDRAND for now, it is too slow */
#undef HAVE_ASM_X86_RDRAND

typedef struct {
	const char *name;
	double	(*rand)(void);
	void	(*seed)(void);
	bool	(*supported)(void);
} stress_monte_carlo_rand_info_t;

typedef struct {
	const char *name;
	double expected;
	double (*method)(const stress_monte_carlo_rand_info_t *info, const uint32_t samples);
} stress_monte_carlo_method_t;

typedef struct {
	double	sum;
	double	count;
} stress_monte_carlo_result_t;

static const stress_help_t help[] = {
	{ NULL,	"monte-carlo N",	"start N workers performing monte-carlo computations" },
	{ NULL,	"monte-carlo-ops N",	"stop after N monte-carlo operations" },
	{ NULL, "monte-carlo-rand R",	"select random number generator [ all | drand48 | getrandom | lcg | pcg32 | mwc32 | mwc64 | random | xorshift ]" },
	{ NULL,	"monte-carlo-samples N","specify number of samples for each computation" },
	{ NULL,	"monte-carlo-method M",	"select computation method [ pi | e | exp | sin | sqrt | squircle ]" },
	{ NULL,	NULL,			NULL }
};

#if (defined(STRESS_ARCH_PPC64) && defined(HAVE_ASM_PPC64_DARN)) ||	\
    (defined(STRESS_ARCH_X86) && defined(HAVE_ASM_X86_RDRAND)) ||	\
    (defined(HAVE_GETRANDOM) && !defined(__sun__)) ||			\
    defined(HAVE_ARC4RANDOM)
static void stress_mc_no_seed(void)
{
}
#endif

#if defined(HAVE_ARC4RANDOM)
static double OPTIMIZE3 stress_mc_arc4_rand(void)
{
	const double scale_u32 = 1.0 / (double)0xffffffffUL;

	return (double)arc4random() * scale_u32;
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_RDRAND)
static double OPTIMIZE3 stress_mc_rdrand_rand(void)
{
	const double scale_u64 = 1.0 / (double)0xffffffffffffffffULL;

	return (double)stress_asm_x86_rdrand() * scale_u64;
}

static bool stress_mc_rdrand_supported(void)
{
	/* Intel CPU? */
	if (!stress_cpu_is_x86())
		return false;

	if (!stress_cpu_x86_has_rdrand())
		return false;
	return true;
}

#endif

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DARN)
static double OPTIMIZE3 stress_mc_darn_rand(void)
{
#if defined(HAVE_BUILTIN_CPU_IS_POWER9)
	const double scale_u32 = 1.0 / (double)0xffffffffUL;

	return (double)stress_asm_ppc64_darn() * scale_u32;
#else
	return 0.1;
#endif
}

static bool stress_mc_darn_supported(void)
{
#if defined(HAVE_BUILTIN_CPU_IS_POWER9)
	if (__builtin_cpu_is("power9"))
		return true;
#endif
#if defined(HAVE_BUILTIN_CPU_IS_POWER10)
	if (__builtin_cpu_is("power10"))
		return true;
#endif
	return false;
}
#endif

static double OPTIMIZE3 stress_mc_mwc32_rand(void)
{
	const double scale_u32 = 1.0 / (double)0xffffffffUL;

	return scale_u32 * (double)stress_mwc32();
}

static void stress_mc_mwc_seed(void)
{
	stress_mwc_reseed();
}

static double OPTIMIZE3 stress_mc_mwc64_rand(void)
{
	const double scale_u64 = 1.0 / (double)0xffffffffffffffffULL;

	return scale_u64 * (double)stress_mwc64();
}

#if defined(HAVE_RANDOM)
static double OPTIMIZE3 stress_mc_random_rand(void)
{
	register const double scale_u32 = 1.0 / (double)0x7fffffff;

	return scale_u32 * (double)random();
}
#endif

#if defined(HAVE_SRANDOM)
static void stress_mc_random_seed(void)
{
	unsigned int seed = (shim_time(NULL) + getpid());

	srandom(seed);
}
#endif

#if defined(HAVE_DRAND48)
static double stress_mc_drand48_rand(void)
{
	return drand48();
}

static void OPTIMIZE3 stress_mc_drand48_seed(void)
{
	register const uint64_t seed64 = (shim_time(NULL) + 1) * getpid();
	unsigned short int seed[3];

	seed[0] = seed64 & 0xffff;
	seed[1] = (seed64 >> 16) & 0xffff;
	seed[2] = (seed64 >> 32) & 0xffff;

	seed48(seed);
}
#endif

#if defined(HAVE_GETRANDOM) &&	\
    !defined(__sun__)
static double OPTIMIZE3 stress_mc_getrandom_rand(void)
{
	static uint64_t buf[16384 / sizeof(uint64_t)];
	static size_t idx = 0;
	register const double scale_u64 = 1.0 / (double)0xffffffffffffffffULL;
	double r;

	if (UNLIKELY(idx == 0)) {
		if (shim_getrandom((void *)buf, sizeof(buf), 0) < 0)
			shim_memset(buf, 0, sizeof(buf));
	}
	r = scale_u64 * (double)buf[idx];
	idx++;
	if (UNLIKELY(idx >= SIZEOF_ARRAY(buf)))
		idx = 0;
	return r;
}
#endif

static uint64_t stress_mc_xorshift_val = 0xf761bb789a2436c9ULL;

static double OPTIMIZE3 stress_mc_xorshift_rand(void)
{
	const double scale_u64 = 1.0 / (double)0xffffffffffffffffULL;

	stress_mc_xorshift_val ^= stress_mc_xorshift_val >> 12;
	stress_mc_xorshift_val ^= stress_mc_xorshift_val << 25;
	stress_mc_xorshift_val ^= stress_mc_xorshift_val >> 27;
	return scale_u64 * (double)(stress_mc_xorshift_val * 0x2545f4914f6cdd1dULL);
}

static void OPTIMIZE3 stress_mc_xorshift_seed(void)
{
	stress_mc_xorshift_val = stress_mwc64();
}

static uint32_t stress_mc_lcg_state = 0xe827139dL;

/*
 *
 *   https://en.wikipedia.org/wiki/Lehmer_random_number_generator
 *   32 bit Paker-Miller Linear Congruential Generator, with division optimization
 */
static double OPTIMIZE3 stress_mc_lcg_rand(void)
{
	register const double scale_u32 = 1.0 / (double)0x7fffffff;
	register uint64_t product = (uint64_t)stress_mc_lcg_state * 48271;
	register uint32_t r = (product & 0x7fffffff) + (product >> 31);

	r = (r & 0x7fffffff) + (r >> 31);
	stress_mc_lcg_state = r;
	return scale_u32 * (double)r;
}

static void stress_mc_lcg_seed(void)
{
	stress_mc_lcg_state = stress_mwc32() | 1;
}

static uint64_t stress_mc_pcg32_state = 0x4d595df4d0f33173ULL;
static uint64_t const stress_mc_pcg32_increment = 1442695040888963407ULL;

static inline ALWAYS_INLINE OPTIMIZE3 uint32_t stress_mc_rotr32(uint32_t x, unsigned r)
{
	return x >> r | x << (-r & 31);
}

static double OPTIMIZE3 stress_mc_pcg32_rand(void)
{
	register uint64_t x = stress_mc_pcg32_state;
	register const unsigned count = (unsigned)(x >> 59);
	register const double scale_u32 = 1.0 / (double)0xffffffff;

	static uint64_t const multiplier = 6364136223846793005u;

	stress_mc_pcg32_state = x * multiplier + stress_mc_pcg32_increment;
	x ^= x >> 18;
	return scale_u32 * (double)stress_mc_rotr32((uint32_t)(x >> 27), count);
}

static void stress_mc_pcg32_seed(void)
{
	stress_mc_pcg32_state = stress_mwc64() + stress_mc_pcg32_increment;
	(void)stress_mc_pcg32_rand();
}

static bool stress_mc_supported(void)
{
	return true;
}

static const stress_monte_carlo_rand_info_t rand_info[] = {
	{ "all",	NULL,				NULL,				stress_mc_supported },
#if defined(HAVE_ARC4RANDOM)
	{ "arc4",	stress_mc_arc4_rand,		stress_mc_no_seed,		stress_mc_supported },
#endif
#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DARN)
	{ "darn",	stress_mc_darn_rand,		stress_mc_no_seed,		stress_mc_darn_supported },
#endif
#if defined(HAVE_DRAND48)
	{ "drand48",	stress_mc_drand48_rand,		stress_mc_drand48_seed,		stress_mc_supported },
#endif
#if defined(HAVE_GETRANDOM) &&	\
    !defined(__sun__)
	{ "getrandom",	stress_mc_getrandom_rand,	stress_mc_no_seed,		stress_mc_supported },
#endif
	{ "lcg",	stress_mc_lcg_rand,		stress_mc_lcg_seed,		stress_mc_supported },
	{ "pcg32",	stress_mc_pcg32_rand,		stress_mc_pcg32_seed,		stress_mc_supported },
	{ "mwc32",	stress_mc_mwc32_rand,		stress_mc_mwc_seed,		stress_mc_supported },
	{ "mwc64",	stress_mc_mwc64_rand,		stress_mc_mwc_seed,		stress_mc_supported },
#if defined(HAVE_RANDOM) &&	\
    defined(HAVE_SRANDOM)
	{ "random",	stress_mc_random_rand,		stress_mc_random_seed,		stress_mc_supported },
#endif
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_RDRAND)
	{ "rdrand",	stress_mc_rdrand_rand,		stress_mc_no_seed,		stress_mc_rdrand_supported },
#endif
	{ "xorshift",	stress_mc_xorshift_rand,	stress_mc_xorshift_seed,	stress_mc_supported },
};

/*
 *  stress_monte_carlo_pi()
 *	compute pi based on area of a circle
 */
static double OPTIMIZE3 stress_monte_carlo_pi(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint64_t pi_count = 0;
	register uint32_t i = samples;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register const double x = info->rand();
			register const double y = info->rand();
			register const double h = (x * x) + (y * y);

			if (h <= 1.0)
				pi_count++;
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return ((double)pi_count) * 4.0 / (double)(samples - i);
}

/*
 *  stress_monte_carlo_e
 *	Euler's number e
 */
static double OPTIMIZE3 stress_monte_carlo_e(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint64_t count = 0;
	register uint32_t i = samples;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register double sum = 0.0;

			while (sum < 1.0)  {
				sum += info->rand();
				count++;
			}
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return (double)count / (double)(samples - i);
}

/*
 *  stress_monte_carlo_sin()
 *	integral of sin(x) for x = 0 to pi
 */
static double OPTIMIZE3 stress_monte_carlo_sin(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint32_t i = samples;
	double sum = 0.0;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register const double theta = info->rand() * M_PI;

			sum += shim_sin(theta);
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return M_PI * (double)sum / (double)(samples - i);
}

/*
 *  stress_monte_carlo_exp()
 *	integral of exp(x * x) for x = 0..1
 */
static double OPTIMIZE3 stress_monte_carlo_exp(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint32_t i = samples;
	double sum = 0.0;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register const double x = info->rand();

			sum += shim_exp(x * x);
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return (double)sum / (double)(samples - i);
}

/*
 *  stress_monte_carlo_sqrt()
 *	integral of sqrt(1 + (x * x * x * x)) for x = 0..1
 */
static double OPTIMIZE3 stress_monte_carlo_sqrt(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint32_t i = samples;
	double sum = 0.0;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register const double x = info->rand();

			sum += shim_sqrt(1.0 + (x * x * x * x));
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return (double)sum / (double)(samples - i);
}

/*
 *  stress_monte_carlo_squircle()
 *	compute area of a squircle, where x^4 + y^4 = r^4
 */
static double OPTIMIZE3 stress_monte_carlo_squircle(
	const stress_monte_carlo_rand_info_t *info,
	const uint32_t samples)
{
	register uint64_t area_count = 0;
	register uint32_t i = samples;

	while (i > 0) {
		register uint32_t j;
		register const uint32_t n = (i > 16384) ? 16384 : (i & 16383);

		for (j = 0; j < n; j++) {
			register const double x = info->rand();
			register const double y = info->rand();
			register const double x2 = x * x;
			register const double y2 = y * y;
			register const double h = (x2 * x2) + (y2 * y2);

			area_count += (h <= 1.0);
		}
		i -= j;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	return (double)(4.0) * (double)area_count / (double)(samples - i);
}


static const stress_monte_carlo_method_t stress_monte_carlo_methods[] = {
	{ "all",	0,			NULL },
	{ "e",		M_E,			stress_monte_carlo_e },
	{ "exp",	1.46265174590718160880,	stress_monte_carlo_exp },
	{ "pi",		M_PI,			stress_monte_carlo_pi },
	{ "sin",	2.0,			stress_monte_carlo_sin },
	{ "sqrt",	1.08942941322482232241,	stress_monte_carlo_sqrt },
	{ "squircle",	3.7081493546,		stress_monte_carlo_squircle },

};

static const char *stress_monte_carlo_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_monte_carlo_methods)) ? stress_monte_carlo_methods[i].name : NULL;
}

static const char *stress_monte_carlo_rand(const size_t i)
{
	return (i < SIZEOF_ARRAY(rand_info)) ? rand_info[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_monte_carlo_method,  "monte-carlo-method",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_monte_carlo_method },
	{ OPT_monte_carlo_rand,    "monte-carlo-rand",    TYPE_ID_SIZE_T_METHOD, 0, 0, stress_monte_carlo_rand },
	{ OPT_monte_carlo_samples, "monte-carlo-samples", TYPE_ID_UINT32, MIN_MONTE_CARLO_SAMPLES, MAX_MONTE_CARLO_SAMPLES, NULL },
	END_OPT,
};

#define METHODS_MAX	SIZEOF_ARRAY(stress_monte_carlo_methods)
#define RANDS_MAX	SIZEOF_ARRAY(rand_info)

static void stress_monto_carlo_by_rand(
	stress_args_t *args,
	uint32_t monte_carlo_samples,
	const size_t rand,
	const size_t method,
	stress_metrics_t metrics[METHODS_MAX][RANDS_MAX],
	stress_monte_carlo_result_t results[METHODS_MAX][RANDS_MAX],
	const bool rands_supported[RANDS_MAX])
{
	if (rand == 0) {
		size_t i;

		/* all random generators */
		for (i = 1; i < RANDS_MAX; i++) {
			if (rands_supported[i]) {
				const double t = stress_time_now();

				results[method][i].sum += stress_monte_carlo_methods[method].method(&rand_info[i], monte_carlo_samples);
				metrics[method][i].duration += (stress_time_now() - t);
				results[method][i].count += 1.0;
				metrics[method][i].count += (double)monte_carlo_samples;
				stress_bogo_inc(args);
			}
		}
	} else {
		const double t = stress_time_now();

		results[method][rand].sum += stress_monte_carlo_methods[method].method(&rand_info[rand], monte_carlo_samples);
		metrics[method][rand].duration += (stress_time_now() - t);
		results[method][rand].count += 1.0;
		metrics[method][rand].count += (double)monte_carlo_samples;

		stress_bogo_inc(args);
	}
}

static void stress_monte_carlo_by_method(
	stress_args_t *args,
	uint32_t monte_carlo_samples,
	const size_t rand,
	const size_t method,
	stress_metrics_t metrics[METHODS_MAX][RANDS_MAX],
	stress_monte_carlo_result_t results[METHODS_MAX][RANDS_MAX],
	const bool rands_supported[RANDS_MAX])
{
	if (method == 0) {
		size_t i;

		/* all methods */
		for (i = 1; i < METHODS_MAX; i++) {
			stress_monto_carlo_by_rand(args, monte_carlo_samples, rand, i, metrics, results, rands_supported);
		}
	} else {
		stress_monto_carlo_by_rand(args, monte_carlo_samples, rand, method, metrics, results, rands_supported);
	}
}

/*
 *  stress_monte_carlo()
 *      stress Intel rdrand instruction
 */
static int stress_monte_carlo(stress_args_t *args)
{
	uint32_t monte_carlo_samples;
	size_t monte_carlo_method,
	monte_carlo_rand = 0;
	stress_metrics_t metrics[METHODS_MAX][RANDS_MAX];
	stress_monte_carlo_result_t results[METHODS_MAX][RANDS_MAX];
	bool rands_supported[RANDS_MAX];
	size_t i, j, idx;

	for (i = 0; i < RANDS_MAX; i++) {
		rands_supported[i] = rand_info[i].supported();
	}

	for (i = 0; i < METHODS_MAX; i++) {
		stress_zero_metrics(metrics[i], RANDS_MAX);
		for (j = 0; j < RANDS_MAX; j++) {
			results[i][j].count = 0.0;
			results[i][j].sum = 0.0;
		}
	}

	monte_carlo_samples = 100000;
	monte_carlo_method = 0;
	monte_carlo_rand = 0;
	(void)stress_get_setting("monte-carlo-method", &monte_carlo_method);
	(void)stress_get_setting("monte-carlo-rand", &monte_carlo_rand);
	if (!stress_get_setting("monte-carlo-samples", &monte_carlo_samples)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			monte_carlo_samples = MAX_MONTE_CARLO_SAMPLES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			monte_carlo_samples = MIN_MONTE_CARLO_SAMPLES;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_monte_carlo_by_method(args, monte_carlo_samples,
			monte_carlo_rand, monte_carlo_method,
			metrics, results, rands_supported);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (idx = 0, i = 1; i < METHODS_MAX; i++) {
		for (j = 1; j < RANDS_MAX; j++) {
			if (metrics[i][j].duration > 0.0) {
				char buf[64];
				const double rate = metrics[i][j].count / metrics[i][j].duration;

				(void)snprintf(buf, sizeof(buf), "samples/sec, %s using %s",
					stress_monte_carlo_methods[i].name, rand_info[j].name);
				stress_metrics_set(args, idx, buf, rate, STRESS_METRIC_GEOMETRIC_MEAN);
				idx++;
			}
		}
	}

	if (stress_instance_zero(args)) {
		pr_block_begin();
		for (i = 1; i < METHODS_MAX; i++) {
			for (j = 1; j < RANDS_MAX; j++) {
				if (results[i][j].count > 0.0) {
					const double result = results[i][j].sum / results[i][j].count;

					pr_dbg("%s: %-8.8s ~ %.13f vs %.13f using %s (average of %.0f runs)\n",
						args->name, stress_monte_carlo_methods[i].name,
						result, stress_monte_carlo_methods[i].expected,
						rand_info[j].name, results[i][j].count);
				}
			}
		}
		pr_block_end();
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_monte_carlo_info = {
	.stressor = stress_monte_carlo,
	.opts = opts,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.verify = VERIFY_NONE,
	.help = help
};
