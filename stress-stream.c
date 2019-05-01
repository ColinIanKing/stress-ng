/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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
 * This stressor is loosely based on the STREAM Sustainable
 * Memory Bandwidth In High Performance Computers tool.
 *   https://www.cs.virginia.edu/stream/
 *   https://www.cs.virginia.edu/stream/FTP/Code/stream.c
 *
 * This is loosely based on a variant of the STREAM benchmark code,
 * so DO NOT submit results based on this as it is intended to
 * stress memory and compute and NOT intended for STREAM accurate
 * tuned or non-tuned benchmarking whatsoever.  I believe this
 * conforms to section 3a, 3b of the original License.
 *
 */
#include "stress-ng.h"

typedef struct {
	const char *name;
        const int advice;
} stream_madvise_info_t;

static const help_t help[] = {
	{ NULL,	"stream N",		"start N workers exercising memory bandwidth" },
	{ NULL,	"stream-ops N",		"stop after N bogo stream operations" },
	{ NULL,	"stream-index",		"specify number of indices into the data (0..3)" },
	{ NULL,	"stream-l3-size N",	"specify the L3 cache size of the CPU" },
	{ NULL,	"stream-madvise M",	"specify mmap'd stream buffer madvise advice" },
	{ NULL,	NULL,                   NULL }
};

static const stream_madvise_info_t stream_madvise_info[] = {
#if defined(HAVE_MADVISE)
#if defined(MADV_HUGEPAGE)
	{ "hugepage",	MADV_HUGEPAGE },
#endif
#if defined(MADV_NOHUGEPAGE)
	{ "nohugepage",	MADV_NOHUGEPAGE },
#endif
#if defined(MADV_NORMAL)
	{ "normal",	MADV_NORMAL },
#endif
#else
	/* No MADVISE, default to normal, ignored */
	{ "normal",	0 },
#endif
        { NULL,         0 },
};

static int stress_set_stream_L3_size(const char *opt)
{
	uint64_t stream_L3_size;

	stream_L3_size = get_uint64_byte(opt);
	check_range_bytes("stream-L3-size", stream_L3_size,
		MIN_STREAM_L3_SIZE, MAX_STREAM_L3_SIZE);
	return set_setting("stream-L3-size", TYPE_ID_UINT64, &stream_L3_size);
}

static int stress_set_stream_madvise(const char *opt)
{
	const stream_madvise_info_t *info;

	for (info = stream_madvise_info; info->name; info++) {
		if (!strcmp(opt, info->name)) {
			set_setting("stream-madvise", TYPE_ID_INT, &info->advice);
			return 0;
		}
	}
	(void)fprintf(stderr, "invalid stream-madvise advice '%s', allowed advice options are:", opt);
	for (info = stream_madvise_info; info->name; info++) {
		(void)fprintf(stderr, " %s", info->name);
        }
	(void)fprintf(stderr, "\n");
	return -1;
}

static int stress_set_stream_index(const char *opt)
{
	uint32_t stream_index;

	stream_index = get_int32(opt);
	check_range("stream-index", stream_index, 0, 3);
	return set_setting("stream-index", TYPE_ID_UINT32, &stream_index);
}

static inline void OPTIMIZE3 stress_stream_copy_index0(
	double *RESTRICT c,
	const double *RESTRICT a,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[i] = a[i];
}

static inline void OPTIMIZE3 stress_stream_copy_index1(
	double *RESTRICT c,
	const double *RESTRICT a,
	size_t *RESTRICT idx1,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx1[i]] = a[idx1[i]];
}

static inline void OPTIMIZE3 stress_stream_copy_index2(
	double *RESTRICT c,
	const double *RESTRICT a,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx1[i]] = a[idx2[i]];
}

static inline void OPTIMIZE3 stress_stream_copy_index3(
	double *RESTRICT c,
	const double *RESTRICT a,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	size_t *RESTRICT idx3,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx3[idx1[i]]] = a[idx2[i]];
}

static inline void OPTIMIZE3 stress_stream_scale_index0(
	double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		b[i] = q * c[i];
}

static inline void OPTIMIZE3 stress_stream_scale_index1(
	double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		b[idx1[i]] = q * c[idx1[i]];
}

static inline void OPTIMIZE3 stress_stream_scale_index2(
	double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		b[idx1[i]] = q * c[idx2[i]];
}

static inline void OPTIMIZE3 stress_stream_scale_index3(
	double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	size_t *RESTRICT idx3,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		b[idx3[idx1[i]]] = q * c[idx2[i]];
}

static inline void OPTIMIZE3 stress_stream_add_index0(
	const double *RESTRICT a,
	const double *RESTRICT b,
	double *RESTRICT c,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[i] = a[i] + b[i];
}

static inline void OPTIMIZE3 stress_stream_add_index1(
	const double *RESTRICT a,
	const double *RESTRICT b,
	double *RESTRICT c,
	size_t *RESTRICT idx1,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx1[i]] = a[idx1[i]] + b[idx1[i]];
}

static inline void OPTIMIZE3 stress_stream_add_index2(
	const double *RESTRICT a,
	const double *RESTRICT b,
	double *RESTRICT c,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx1[i]] = a[idx2[i]] + b[idx1[i]];
}

static inline void OPTIMIZE3 stress_stream_add_index3(
	const double *RESTRICT a,
	const double *RESTRICT b,
	double *RESTRICT c,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	size_t *RESTRICT idx3,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		c[idx1[i]] = a[idx2[i]] + b[idx3[i]];
}

static inline void OPTIMIZE3 stress_stream_triad_index0(
	double *RESTRICT a,
	const double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		a[i] = b[i] + (c[i] * q);
}

static inline void OPTIMIZE3 stress_stream_triad_index1(
	double *RESTRICT a,
	const double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		a[idx1[i]] = b[idx1[i]] + (c[idx1[i]] * q);
}

static inline void OPTIMIZE3 stress_stream_triad_index2(
	double *RESTRICT a,
	const double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		a[idx1[i]] = b[idx2[i]] + (c[idx1[i]] * q);
}

static inline void OPTIMIZE3 stress_stream_triad_index3(
	double *RESTRICT a,
	const double *RESTRICT b,
	const double *RESTRICT c,
	const double q,
	size_t *RESTRICT idx1,
	size_t *RESTRICT idx2,
	size_t *RESTRICT idx3,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++)
		a[idx1[i]] = b[idx2[i]] + (c[idx3[i]] * q);
}

static void stress_stream_init_data(
	double *RESTRICT data,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++)
		data[i] = (double)mwc32() / (double)mwc64();
}

static inline void *stress_stream_mmap(const args_t *args, uint64_t sz)
{
	void *ptr;

	ptr = mmap(NULL, (size_t)sz, PROT_READ | PROT_WRITE,
#if defined(MAP_POPULATE)
		MAP_POPULATE |
#endif
#if defined(HAVE_MADVISE)
		MAP_PRIVATE |
#else
		MAP_SHARED |
#endif
		MAP_ANONYMOUS, -1, 0);
	/* Coverity Scan believes NULL can be returned, doh */
	if (!ptr || (ptr == MAP_FAILED)) {
		pr_err("%s: cannot allocate %" PRIu64 " bytes\n",
			args->name, sz);
		ptr = MAP_FAILED;
	} else {
#if defined(HAVE_MADVISE)
		int ret, advice = MADV_NORMAL;

		(void)get_setting("stream-madvise", &advice);

		ret = madvise(ptr, sz, advice);
		(void)ret;
#endif
	}
	return ptr;
}

static inline uint64_t get_stream_L3_size(const args_t *args)
{
	uint64_t cache_size = MEM_CACHE_SIZE;
#if defined(__linux__)
	cpus_t *cpu_caches;
	cpu_cache_t *cache = NULL;
	uint16_t max_cache_level;

	cpu_caches = get_all_cpu_cache_details();
	if (!cpu_caches) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache details\n", args->name);
		return cache_size;
	}
	max_cache_level = get_max_cache_level(cpu_caches);
	if ((max_cache_level > 0) && (max_cache_level < 3) && (!args->instance))
		pr_inf("%s: no L3 cache, using L%" PRIu16 " size instead\n",
			args->name, max_cache_level);

	cache = get_cpu_cache(cpu_caches, max_cache_level);
	if (!cache) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as no suitable "
				"cache found\n", args->name);
		free_cpu_caches(cpu_caches);
		return cache_size;
	}
	if (!cache->size) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache size\n", args->name);
		free_cpu_caches(cpu_caches);
		return cache_size;
	}
	cache_size = cache->size;

	free_cpu_caches(cpu_caches);
#else
	if (!args->instance)
		pr_inf("%s: using built-in defaults as unable to "
			"determine cache details\n", args->name);
#endif
	return cache_size;
}

static void stress_stream_init_index(
	size_t *RESTRICT idx,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++)
		idx[i] = i;

	for (i = 0; i < n; i++) {
		register uint64_t j = mwc64() % n;
		register uint64_t tmp;

		tmp = idx[i];
		idx[i] = idx[j];
		idx[j] = tmp;
	}
}

/*
 *  stress_stream()
 *	stress cache/memory/CPU with stream stressors
 */
static int stress_stream(const args_t *args)
{
	int rc = EXIT_FAILURE;
	double *a, *b, *c;
	size_t *idx1 = NULL, *idx2 = NULL, *idx3 = NULL;
	const double q = 3.0;
	double mb_rate, mb, fp_rate, fp, t1, t2, dt;
	uint32_t stream_index = 0;
	uint64_t L3, sz, n, sz_idx;
	uint64_t stream_L3_size = DEFAULT_STREAM_L3_SIZE;
	bool guess = false;

	if (get_setting("stream-L3-size", &stream_L3_size))
		L3 = stream_L3_size;
	else
		L3 = get_stream_L3_size(args);

	(void)get_setting("stream-index", &stream_index);

	/* Have to take a hunch and badly guess size */
	if (!L3) {
		guess = true;
		L3 = stress_get_processors_configured() * DEFAULT_STREAM_L3_SIZE;
	}

	if (args->instance == 0) {
		pr_inf("%s: stressor loosely based on a variant of the "
			"STREAM benchmark code\n", args->name);
		pr_inf("%s: do NOT submit any of these results "
			"to the STREAM benchmark results\n", args->name);
		if (guess) {
			pr_inf("%s: cannot determine CPU L3 cache size, "
				"defaulting to %" PRIu64 "K\n",
				args->name, L3 / 1024);
		} else {
			pr_inf("%s: Using CPU cache size of %" PRIu64 "K\n",
				args->name, L3 / 1024);
		}
	}

	/* ..and shared amongst all the STREAM stressor instances */
	L3 /= args->num_instances;
	if (L3 < args->page_size)
		L3 = args->page_size;

	/*
	 *  Each array must be at least 4 x the
	 *  size of the L3 cache
	 */
	sz = (L3 * 4);
	n = sz / sizeof(*a);

	a = stress_stream_mmap(args, sz);
	if (a == MAP_FAILED)
		goto err_a;
	b = stress_stream_mmap(args, sz);
	if (b == MAP_FAILED)
		goto err_b;
	c = stress_stream_mmap(args, sz);
	if (c == MAP_FAILED)
		goto err_c;

	sz_idx = n * sizeof(size_t);
	switch (stream_index) {
	case 3:
		idx3 = stress_stream_mmap(args, sz_idx);
		if (idx3 == MAP_FAILED)
			goto err_idx3;
		stress_stream_init_index(idx3, n);
		CASE_FALLTHROUGH;
	case 2:
		idx2 = stress_stream_mmap(args, sz_idx);
		if (idx2 == MAP_FAILED)
			goto err_idx2;
		stress_stream_init_index(idx2, n);
		CASE_FALLTHROUGH;
	case 1:
		idx1 = stress_stream_mmap(args, sz_idx);
		if (idx1 == MAP_FAILED)
			goto err_idx1;
		stress_stream_init_index(idx1, n);
		CASE_FALLTHROUGH;
	case 0:
	default:
		break;
	}

	stress_stream_init_data(a, n);
	stress_stream_init_data(b, n);
	stress_stream_init_data(c, n);

	t1 = time_now();
	do {
		switch (stream_index) {
		case 3:
			stress_stream_copy_index3(c, a, idx1, idx2, idx3, n);
			stress_stream_scale_index3(b, c, q, idx1, idx2, idx3, n);
			stress_stream_add_index3(c, b, a, idx1, idx2, idx3, n);
			stress_stream_triad_index3(a, b, c, q, idx1, idx2, idx3, n);
			break;
		case 2:
			stress_stream_copy_index2(c, a, idx1, idx2, n);
			stress_stream_scale_index2(b, c, q, idx1, idx2, n);
			stress_stream_add_index2(c, b, a, idx1, idx2, n);
			stress_stream_triad_index2(a, b, c, q, idx1, idx2, n);
			break;
		case 1:
			stress_stream_copy_index1(c, a, idx1, n);
			stress_stream_scale_index1(b, c, q, idx1, n);
			stress_stream_add_index1(c, b, a, idx1, n);
			stress_stream_triad_index1(a, b, c, q, idx1, n);
			break;
		case 0:
		default:
			stress_stream_copy_index0(c, a, n);
			stress_stream_scale_index0(b, c, q, n);
			stress_stream_add_index0(c, b, a, n);
			stress_stream_triad_index0(a, b, c, q, n);
			break;
		}
		inc_counter(args);
	} while (keep_stressing());
	t2 = time_now();

	mb = ((double)(get_counter(args) * 10) * (double)sz) / (double)MB;
	fp = ((double)(get_counter(args) * 4) * (double)sz) / (double)MB;
	dt = t2 - t1;
	if (dt >= 4.5) {
		mb_rate = mb / (dt);
		fp_rate = fp / (dt);
		pr_inf("%s: memory rate: %.2f MB/sec, %.2f Mflop/sec"
			" (instance %" PRIu32 ")\n",
			args->name, mb_rate, fp_rate, args->instance);
	} else {
		if (args->instance == 0)
			pr_inf("%s: run too short to determine memory rate\n", args->name);
	}

	rc = EXIT_SUCCESS;

	if (idx3)
		(void)munmap((void *)idx3, sz_idx);
err_idx3:
	if (idx2)
		(void)munmap((void *)idx2, sz_idx);
err_idx2:
	if (idx1)
		(void)munmap((void *)idx1, sz_idx);
err_idx1:
	(void)munmap((void *)c, sz);
err_c:
	(void)munmap((void *)b, sz);
err_b:
	(void)munmap((void *)a, sz);
err_a:

	return rc;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_stream_index,	stress_set_stream_index },
	{ OPT_stream_l3_size,	stress_set_stream_L3_size },
	{ OPT_stream_madvise,	stress_set_stream_madvise },
	{ 0,			NULL }
};

stressor_info_t stress_stream_info = {
	.stressor = stress_stream,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
