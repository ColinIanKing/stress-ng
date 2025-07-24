/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-cpu.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-nt-store.h"
#include "core-numa.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#include <math.h>

#define MIN_STREAM_L3_SIZE	(4 * KB)
#define MAX_STREAM_L3_SIZE	(MAX_MEM_LIMIT)
#define DEFAULT_STREAM_L3_SIZE	(4 * MB)

#if defined(HAVE_NT_STORE_DOUBLE)
#define NT_STORE(dst, src)		stress_nt_store_double(&dst, src)
#endif

#define STORE(dst, src)			dst = src

typedef struct {
	const char *name;
	const int advice;
} stress_stream_madvise_info_t;

static const stress_help_t help[] = {
	{ NULL,	"stream N",		"start N workers exercising memory bandwidth" },
	{ NULL,	"stream-index N",	"specify number of indices into the data (0..3)" },
	{ NULL,	"stream-l3-size N",	"specify the L3 cache size of the CPU" },
	{ NULL,	"stream-madvise M",	"specify mmap'd stream buffer madvise advice" },
	{ NULL,	"stream-mlock",		"attempt to mlock pages into memory" },
	{ NULL, "stream-prefetch",	"use prefetching (where available)" },
	{ NULL,	"stream-ops N",		"stop after N bogo stream operations" },
	{ NULL,	NULL,                   NULL }
};

static const stress_stream_madvise_info_t stream_madvise_info[] = {
#if !defined(HAVE_MADVISE)
	/* No MADVISE, default to normal, ignored */
	{ "normal",	0 },
#else
#if defined(MADV_HUGEPAGE)
	{ "hugepage",	MADV_HUGEPAGE },
#endif
#if defined(MADV_NOHUGEPAGE)
	{ "nohugepage",	MADV_NOHUGEPAGE },
#endif
#if defined(MADV_COLLAPSE)
	{ "collapse",	MADV_COLLAPSE },
#endif
#if defined(MADV_NORMAL)
	{ "normal",	MADV_NORMAL },
#endif
#endif
};

/*
 *  stress_stream_checksum_to_hexstr()
 *	turn a double into a hexadecimal string making zero assumptions about
 *	the size of a double since this maybe arch specific.
 */
static void stress_stream_checksum_to_hexstr(char *str, const size_t len, const double checksum)
{
	const unsigned char *ptr = (const unsigned char *)&checksum;
	size_t i, j;

	for (i = 0, j = 0; (i < sizeof(checksum)) && (j < len); i++, j += 2) {
		(void)snprintf(str + j, 3, "%2.2x", ptr[i]);
	}
	str[j] = '\0';
}

#define STRESS_STREAM_COPY_INDEX0(x)				\
static inline void x (						\
	double *const RESTRICT c,				\
	const double *const RESTRICT a,				\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		STORE(cv[i + 0], a[i + 0]);			\
		STORE(cv[i + 1], a[i + 1]);			\
		STORE(cv[i + 2], a[i + 2]);			\
		STORE(cv[i + 3], a[i + 3]);			\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a));		\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += 0.0;						\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_COPY_INDEX0(stress_stream_copy_index0_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_COPY_INDEX0(stress_stream_copy_index0)
#else
STRESS_STREAM_COPY_INDEX0(stress_stream_copy_index0)
#endif

#if defined(HAVE_NT_STORE_DOUBLE)
#define STRESS_STREAM_COPY_INDEX0_NT(x)				\
static inline void 		x (				\
	double *const RESTRICT c,				\
	const double *const RESTRICT a,				\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		NT_STORE(c[i + 0], a[i + 0]);			\
		NT_STORE(c[i + 1], a[i + 1]);			\
		NT_STORE(c[i + 2], a[i + 2]);			\
		NT_STORE(c[i + 3], a[i + 3]);			\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a));		\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += 0.0;						\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_COPY_INDEX0_NT(stress_stream_copy_index0_nt_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_COPY_INDEX0_NT(stress_stream_copy_index0_nt)
#else
STRESS_STREAM_COPY_INDEX0_NT(stress_stream_copy_index0_nt)
#endif

#endif

#define STRESS_STREAM_COPY_INDEX1(x)				\
static inline void x (						\
	double *const RESTRICT c,				\
	const double *const RESTRICT a,				\
	const size_t *const RESTRICT idx1,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		const size_t idx = idx1[i];			\
								\
		STORE(cv[idx], a[idx]);				\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*idx1));				\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += 0.0;						\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_COPY_INDEX1(stress_stream_copy_index1_prefetch)

STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_COPY_INDEX1(stress_stream_copy_index1)
#else
STRESS_STREAM_COPY_INDEX1(stress_stream_copy_index1)
#endif

#define STRESS_STREAM_COPY_INDEX2(x)				\
static inline void x(						\
	double *const RESTRICT c,				\
	const double *const RESTRICT a,				\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const uint64_t n,					\
	double *rd_bytes,					\
	double *wr_bytes,					\
	double *fp_ops)						\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(cv[idx1[i]], a[idx2[i]]);			\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*idx1) + sizeof(*idx2));		\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += 0.0;						\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_COPY_INDEX2(stress_stream_copy_index2_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_COPY_INDEX2(stress_stream_copy_index2)
#else
STRESS_STREAM_COPY_INDEX2(stress_stream_copy_index2)
#endif

#define STRESS_STREAM_COPY_INDEX3(x)				\
static inline void x(						\
	double *const RESTRICT c,				\
	const double *const RESTRICT a,				\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const size_t *const RESTRICT idx3,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(cv[idx3[idx1[i]]], a[idx2[i]]);		\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*idx1) + sizeof(*idx2) +		\
			sizeof(*idx3));				\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += 0.0;						\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_COPY_INDEX3(stress_stream_copy_index3_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_COPY_INDEX3(stress_stream_copy_index3)
#else
STRESS_STREAM_COPY_INDEX3(stress_stream_copy_index3)
#endif

#define STRESS_STREAM_SCALE_INDEX0(x)				\
static inline void x(						\
	double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT bv = b;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		STORE(bv[i + 0], q * c[i + 0]);			\
		STORE(bv[i + 1], q * c[i + 1]);			\
		STORE(bv[i + 2], q * c[i + 2]);			\
		STORE(bv[i + 3], q * c[i + 3]);			\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*c));		\
	*wr_bytes += (double)n * (double)(sizeof(*b));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_SCALE_INDEX0(stress_stream_scale_index0_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_SCALE_INDEX0(stress_stream_scale_index0)
#else
STRESS_STREAM_SCALE_INDEX0(stress_stream_scale_index0)
#endif

#if defined(HAVE_NT_STORE_DOUBLE)
#define STRESS_STREAM_SCALE_INDEX0_NT(x)			\
static inline void x(						\
	double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		NT_STORE(b[i + 0], q * c[i + 0]);		\
		NT_STORE(b[i + 1], q * c[i + 1]);		\
		NT_STORE(b[i + 2], q * c[i + 2]);		\
		NT_STORE(b[i + 3], q * c[i + 3]);		\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*c));		\
	*wr_bytes += (double)n * (double)(sizeof(*b));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_SCALE_INDEX0_NT(stress_stream_scale_index0_nt_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_SCALE_INDEX0_NT(stress_stream_scale_index0_nt)
#else
STRESS_STREAM_SCALE_INDEX0_NT(stress_stream_scale_index0_nt)
#endif

#endif

#define STRESS_STREAM_SCALE_INDEX1(x)				\
static inline void x(						\
	double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT bv = b;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		const size_t idx = idx1[i];			\
								\
		STORE(bv[idx], q * c[idx]);			\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*c) +		\
			sizeof(*idx1));				\
	*wr_bytes += (double)n * (double)(sizeof(*b));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_SCALE_INDEX1(stress_stream_scale_index1_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_SCALE_INDEX1(stress_stream_scale_index1)
#else
STRESS_STREAM_SCALE_INDEX1(stress_stream_scale_index1)
#endif

#define STRESS_STREAM_SCALE_INDEX2(x)				\
static inline void x(						\
	double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT bv = b;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(bv[idx1[i]], q * c[idx2[i]]);		\
								\
	*rd_bytes += (double)n * (double)(sizeof(*c) +		\
			sizeof(*idx1) + sizeof(*idx2));		\
	*wr_bytes += (double)n * (double)(sizeof(*b));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_SCALE_INDEX2(stress_stream_scale_index2_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_SCALE_INDEX2(stress_stream_scale_index2)
#else
STRESS_STREAM_SCALE_INDEX2(stress_stream_scale_index2)
#endif

#define STRESS_STREAM_SCALE_INDEX3(x)				\
static inline void x(						\
	double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const size_t *const RESTRICT idx3,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT bv = b;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(bv[idx3[idx1[i]]], q * c[idx2[i]]);	\
								\
	*rd_bytes += (double)n * (double)(sizeof(*c) +		\
			sizeof(*idx1) + sizeof(*idx2) +		\
			sizeof(*idx3));				\
	*wr_bytes += (double)n * (double)(sizeof(*b));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_SCALE_INDEX3(stress_stream_scale_index3_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_SCALE_INDEX3(stress_stream_scale_index3)
#else
STRESS_STREAM_SCALE_INDEX3(stress_stream_scale_index3)
#endif

#define STRESS_STREAM_ADD_INDEX0(x)				\
static inline void x(						\
	const double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	double *const RESTRICT c,				\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		STORE(cv[i + 0], a[i + 0] + b[i + 0]);		\
		STORE(cv[i + 1], a[i + 1] + b[i + 1]);		\
		STORE(cv[i + 2], a[i + 2] + b[i + 2]);		\
		STORE(cv[i + 3], a[i + 3] + b[i + 3]);		\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*b));				\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_ADD_INDEX0(stress_stream_add_index0_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_ADD_INDEX0(stress_stream_add_index0)
#else
STRESS_STREAM_ADD_INDEX0(stress_stream_add_index0)
#endif

#if defined(HAVE_NT_STORE_DOUBLE)
#define STRESS_STREAM_ADD_INDEX0_NT(x)				\
static inline void x(						\
	const double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	double *const RESTRICT c,				\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		NT_STORE(c[i + 0], a[i + 0] + b[i + 0]);	\
		NT_STORE(c[i + 1], a[i + 1] + b[i + 1]);	\
		NT_STORE(c[i + 2], a[i + 2] + b[i + 2]);	\
		NT_STORE(c[i + 3], a[i + 3] + b[i + 3]);	\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*b));				\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_ADD_INDEX0_NT(stress_stream_add_index0_nt_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_ADD_INDEX0_NT(stress_stream_add_index0_nt)
#else
STRESS_STREAM_ADD_INDEX0_NT(stress_stream_add_index0_nt)
#endif

#endif

#define STRESS_STREAM_ADD_INDEX1(x)				\
static inline void x(						\
	const double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	double *const RESTRICT c,				\
	const size_t *const RESTRICT idx1,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		const size_t idx = idx1[i];			\
								\
		STORE(cv[idx], a[idx] + b[idx]);		\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*b) + sizeof(*idx1));		\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_ADD_INDEX1(stress_stream_add_index1_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_ADD_INDEX1(stress_stream_add_index1)
#else
STRESS_STREAM_ADD_INDEX1(stress_stream_add_index1)
#endif

#define STRESS_STREAM_ADD_INDEX2(x)				\
static inline void x(						\
	const double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	double *const RESTRICT c,				\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		const size_t idx = idx1[i];			\
								\
		STORE(cv[idx], a[idx2[i]] + b[idx]);		\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*b) + sizeof(*idx1) +		\
			sizeof(*idx2));				\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_ADD_INDEX2(stress_stream_add_index2_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_ADD_INDEX2(stress_stream_add_index2)
#else
STRESS_STREAM_ADD_INDEX2(stress_stream_add_index2)
#endif

#define STRESS_STREAM_ADD_INDEX3(x)				\
static inline void x(						\
	const double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	double *const RESTRICT c,				\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const size_t *const RESTRICT idx3,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT cv = c;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(cv[idx1[i]], a[idx2[i]] + b[idx3[i]]);	\
								\
	*rd_bytes += (double)n * (double)(sizeof(*a) +		\
			sizeof(*b) + sizeof(*idx1) + 		\
			sizeof(*idx2) + sizeof(*idx3));		\
	*wr_bytes += (double)n * (double)(sizeof(*c));		\
	*fp_ops += (double)n;					\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_ADD_INDEX3(stress_stream_add_index3_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_ADD_INDEX3(stress_stream_add_index3)
#else
STRESS_STREAM_ADD_INDEX3(stress_stream_add_index3)
#endif

#define STRESS_STREAM_TRIAD_INDEX0(x)				\
static inline void x(						\
	double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT av = a;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		STORE(av[i + 0], b[i + 0] + (c[i + 0] * q));	\
		STORE(av[i + 1], b[i + 1] + (c[i + 1] * q));	\
		STORE(av[i + 2], b[i + 2] + (c[i + 2] * q));	\
		STORE(av[i + 3], b[i + 3] + (c[i + 3] * q));	\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*b) +		\
			sizeof(*c));				\
	*wr_bytes += (double)n * (double)(sizeof(*a));		\
	*fp_ops += (double)n * 2.0;				\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_TRIAD_INDEX0(stress_stream_triad_index0_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_TRIAD_INDEX0(stress_stream_triad_index0)
#else
STRESS_STREAM_TRIAD_INDEX0(stress_stream_triad_index0)
#endif

#if defined(HAVE_NT_STORE_DOUBLE)
#define STRESS_STREAM_TRIAD_INDEX0_NT(x)			\
static inline void x(						\
	double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i += 4) {				\
		NT_STORE(a[i + 0], b[i + 0] + (c[i + 0] * q));	\
		NT_STORE(a[i + 1], b[i + 1] + (c[i + 1] * q));	\
		NT_STORE(a[i + 2], b[i + 2] + (c[i + 2] * q));	\
		NT_STORE(a[i + 3], b[i + 3] + (c[i + 3] * q));	\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*b) +		\
			sizeof(*c));				\
	*wr_bytes += (double)n * (double)(sizeof(*a));		\
	*fp_ops += (double)n * 2.0;				\
}
#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_TRIAD_INDEX0_NT(stress_stream_triad_index0_nt_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_TRIAD_INDEX0_NT(stress_stream_triad_index0_nt)
#else
STRESS_STREAM_TRIAD_INDEX0_NT(stress_stream_triad_index0_nt)
#endif

#endif

#define STRESS_STREAM_TRIAD_INDEX1(x)				\
static inline void x(						\
	double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT av = a;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		size_t idx = idx1[i];				\
								\
		STORE(av[idx], b[idx] + (c[idx] * q));		\
	}							\
	*rd_bytes += (double)n * (double)(sizeof(*b) +		\
			sizeof(*c) + sizeof(*idx1));		\
	*wr_bytes += (double)n * (double)(sizeof(*a));		\
	*fp_ops += (double)n * 2.0;				\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_TRIAD_INDEX1(stress_stream_triad_index1_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_TRIAD_INDEX1(stress_stream_triad_index1)
#else
STRESS_STREAM_TRIAD_INDEX1(stress_stream_triad_index1)
#endif

#define STRESS_STREAM_TRIAD_INDEX2(x)				\
static inline void x(						\
	double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT av = a;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++) {				\
		const size_t idx = idx1[i];			\
								\
		STORE(av[idx], b[idx2[i]] + (c[idx] * q));	\
	}							\
								\
	*rd_bytes += (double)n * (double)(sizeof(*b) +		\
			sizeof(*c) + sizeof(*idx1) +		\
			sizeof(*idx2));				\
	*wr_bytes += (double)n * (double)(sizeof(*a));		\
	*fp_ops += (double)n * 2.0;				\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_TRIAD_INDEX2(stress_stream_triad_index2_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_TRIAD_INDEX2(stress_stream_triad_index2)
#else
STRESS_STREAM_TRIAD_INDEX2(stress_stream_triad_index2)
#endif

#define STRESS_STREAM_TRIAD_INDEX3(x)				\
static inline void x(						\
	double *const RESTRICT a,				\
	const double *const RESTRICT b,				\
	const double *const RESTRICT c,				\
	const double q,						\
	const size_t *const RESTRICT idx1,			\
	const size_t *const RESTRICT idx2,			\
	const size_t *const RESTRICT idx3,			\
	const uint64_t n,					\
	double *const RESTRICT rd_bytes,			\
	double *const RESTRICT wr_bytes,			\
	double *const RESTRICT fp_ops)				\
{								\
	register uint64_t i;					\
	register double volatile *RESTRICT av = a;		\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < n; i++)					\
		STORE(av[idx1[i]], b[idx2[i]] +			\
			(c[idx3[i]] * q));			\
								\
	*rd_bytes += (double)n * (double)(sizeof(*b) +		\
			sizeof(*c) + sizeof(*idx1) +		\
			sizeof(*idx2) + sizeof(*idx3));		\
	*wr_bytes += (double)n * (double)(sizeof(*a));		\
	*fp_ops += (double)n * 2.0;				\
}

#if defined(HAVE_PRAGMA_PREFETCH)
STRESS_PRAGMA_PREFETCH
STRESS_STREAM_TRIAD_INDEX3(stress_stream_triad_index3_prefetch)
STRESS_PRAGMA_NOPREFETCH
STRESS_STREAM_TRIAD_INDEX3(stress_stream_triad_index3)
#else
STRESS_STREAM_TRIAD_INDEX3(stress_stream_triad_index3)
#endif

static inline ALWAYS_INLINE void stress_stream_init_data(
	double *const RESTRICT a,
	double *const RESTRICT b,
	double *const RESTRICT c,
	const uint64_t n)
{
	register const double divisor = 1.0 / (double)(4294967296ULL);
	register const double delta = (double)stress_mwc32() * divisor;

	register const uint32_t r = stress_mwc32();
	register double v = (double)r * divisor;
	register double *ptr, *ptr_end;

PRAGMA_UNROLL_N(4)
	for (ptr = a, ptr_end = a + n; ptr < ptr_end; ptr += 4) {
		STORE(ptr[0], v);
		STORE(ptr[1], v);
		STORE(ptr[2], v);
		STORE(ptr[3], v);
		v += delta;
	}

PRAGMA_UNROLL_N(4)
	for (ptr = b, ptr_end = b + n; ptr < ptr_end; ptr += 4) {
		STORE(ptr[0], v);
		STORE(ptr[1], v);
		STORE(ptr[2], v);
		STORE(ptr[3], v);
		v += delta;
	}

PRAGMA_UNROLL_N(4)
	for (ptr = c, ptr_end = c + n; ptr < ptr_end; ptr += 4) {
		STORE(ptr[0], v);
		STORE(ptr[1], v);
		STORE(ptr[2], v);
		STORE(ptr[3], v);
		v += delta;
	}
}

static double TARGET_CLONES OPTIMIZE3 stress_stream_checksum_data(
	const double *const RESTRICT a,
	const double *const RESTRICT b,
	const double *const RESTRICT c,
	const uint64_t n)
{
	double checksum = 0.0;
	register uint64_t i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < n; i++) {
		checksum += a[i] + b[i] + c[i];
	}
	return checksum;
}

static inline void *stress_stream_mmap(
	stress_args_t *args,
	const uint64_t sz,
	const bool stream_mlock)
{
	void *ptr;

	ptr = stress_mmap_populate(NULL, (size_t)sz, PROT_READ | PROT_WRITE,
#if defined(HAVE_MADVISE)
		MAP_PRIVATE |
#else
		MAP_SHARED |
#endif
		MAP_ANONYMOUS, -1, 0);
	/* Coverity Scan believes NULL can be returned, doh */
	if (!ptr || (ptr == MAP_FAILED)) {
		pr_err("%s: failed to mmap %" PRIu64 " bytes%s, errno=%d (%s)\n",
			args->name, sz,
			stress_get_memfree_str(), errno, strerror(errno));
		ptr = MAP_FAILED;
	} else {
		stress_set_vma_anon_name(ptr, sz, "stream-buffer");
		if (stream_mlock)
			(void)shim_mlock(ptr, (size_t)sz);
#if defined(HAVE_MADVISE)
		size_t stream_madvise;
		int advice = MADV_NORMAL;

		if (stress_get_setting("stream-madvise", &stream_madvise))
			advice = stream_madvise_info[stream_madvise].advice;

		VOID_RET(int, madvise(ptr, (size_t)sz, advice));
#else
		UNEXPECTED
#endif
	}
	return ptr;
}

static inline uint64_t get_stream_L3_size(stress_args_t *args)
{
	uint64_t cache_size = 2 * MB;
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint16_t max_cache_level;
	const int numa_nodes = stress_numa_nodes();

	cpu_caches = stress_cpu_cache_get_all_details();
	if (!cpu_caches) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache details\n", args->name);
		goto report_size;
	}
	max_cache_level = stress_cpu_cache_get_max_level(cpu_caches);
	if ((max_cache_level > 0) && (max_cache_level < 3) && (!args->instance))
		pr_inf("%s: no L3 cache, using L%" PRIu16 " size instead\n",
			args->name, max_cache_level);

	cache = stress_cpu_cache_get(cpu_caches, max_cache_level);
	if (!cache) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as no suitable "
				"cache found\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		goto report_size;
	}
	if (!cache->size) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache size\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		goto report_size;
	}
	cache_size = cache->size;

	stress_free_cpu_caches(cpu_caches);

report_size:
	cache_size *= numa_nodes;
	if (stress_instance_zero(args) && (numa_nodes > 1))
		pr_inf("%s: scaling L3 cache size by number of numa nodes %d to %" PRIu64 "K\n",
			args->name, numa_nodes, cache_size / 1024);
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
		register const uint64_t j = stress_mwc64modn(n);
		register const uint64_t tmp = idx[i];

		idx[i] = idx[j];
		idx[j] = tmp;
	}
}

static void OPTIMIZE3 TARGET_CLONES stress_stream_exercise(
	double *const RESTRICT dt,
	double *const RESTRICT a,
	double *const RESTRICT b,
	double *const RESTRICT c,
	const size_t *const RESTRICT idx1,
	const size_t *const RESTRICT idx2,
	const size_t *const RESTRICT idx3,
	double *const RESTRICT rd_bytes,
	double *const RESTRICT wr_bytes,
	double *const RESTRICT fp_ops,
	const double q,
	const uint64_t n,
	const uint32_t stream_index,
	const bool has_sse2,
	const bool stream_prefetch)
{
	double t1, t2;

#if !defined(HAVE_PRAGMA_PREFETCH)
	(void)stream_prefetch;
#endif

	switch (stream_index) {
	case 3:
#if defined(HAVE_PRAGMA_PREFETCH)
		if (stream_prefetch) {
			t1 = stress_time_now();
			stress_stream_copy_index3_prefetch(c, a, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index3_prefetch(b, c, q, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index3_prefetch(c, b, a, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index3_prefetch(a, b, c, q, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
			t2 = stress_time_now();
			break;
		}
#endif
		t1 = stress_time_now();
		stress_stream_copy_index3(c, a, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_scale_index3(b, c, q, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_add_index3(c, b, a, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_triad_index3(a, b, c, q, idx1, idx2, idx3, n, rd_bytes, wr_bytes, fp_ops);
		t2 = stress_time_now();
		break;
	case 2:
#if defined(HAVE_PRAGMA_PREFETCH)
		if (stream_prefetch) {
			t1 = stress_time_now();
			stress_stream_copy_index2_prefetch(c, a, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index2_prefetch(b, c, q, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index2_prefetch(c, b, a, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index2_prefetch(a, b, c, q, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
			t2 = stress_time_now();
			break;
		}
#endif
		t1 = stress_time_now();
		stress_stream_copy_index2(c, a, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_scale_index2(b, c, q, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_add_index2(c, b, a, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_triad_index2(a, b, c, q, idx1, idx2, n, rd_bytes, wr_bytes, fp_ops);
		t2 = stress_time_now();
		break;
	case 1:
#if defined(HAVE_PRAGMA_PREFETCH)
		if (stream_prefetch) {
			t1 = stress_time_now();
			stress_stream_copy_index1_prefetch(c, a, idx1, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index1_prefetch(b, c, q, idx1, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index1_prefetch(c, b, a, idx1, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index1_prefetch(a, b, c, q, idx1, n, rd_bytes, wr_bytes, fp_ops);
			t2 = stress_time_now();
			break;
		}
#endif
		t1 = stress_time_now();
		stress_stream_copy_index1(c, a, idx1, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_scale_index1(b, c, q, idx1, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_add_index1(c, b, a, idx1, n, rd_bytes, wr_bytes, fp_ops);
		stress_stream_triad_index1(a, b, c, q, idx1, n, rd_bytes, wr_bytes, fp_ops);
		t2 = stress_time_now();
		break;
	case 0:
	default:
#if defined(HAVE_NT_STORE_DOUBLE)
		if (has_sse2) {
#if defined(HAVE_PRAGMA_PREFETCH)
			if (stream_prefetch) {
				t1 = stress_time_now();
				stress_stream_copy_index0_nt_prefetch(c, a, n, rd_bytes, wr_bytes, fp_ops);
				stress_stream_scale_index0_nt_prefetch(b, c, q, n, rd_bytes, wr_bytes, fp_ops);
				stress_stream_add_index0_nt_prefetch(c, b, a, n,  rd_bytes, wr_bytes, fp_ops);
				stress_stream_triad_index0_nt_prefetch(a, b, c, q, n, rd_bytes, wr_bytes, fp_ops);
				t2 = stress_time_now();
				break;
			}
#endif
			t1 = stress_time_now();
			stress_stream_copy_index0_nt(c, a, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index0_nt(b, c, q, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index0_nt(c, b, a, n,  rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index0_nt(a, b, c, q, n, rd_bytes, wr_bytes, fp_ops);
			t2 = stress_time_now();
			break;
		}
#else
		(void)has_sse2;
#endif
		if (stream_prefetch) {
			t1 = stress_time_now();
#if defined(HAVE_PRAGMA_PREFETCH)
			stress_stream_copy_index0_prefetch(c, a, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index0_prefetch(b, c, q, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index0_prefetch(c, b, a, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index0_prefetch(a, b, c, q, n, rd_bytes, wr_bytes, fp_ops);
#endif
			t2 = stress_time_now();
		} else {
			t1 = stress_time_now();
			stress_stream_copy_index0(c, a, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_scale_index0(b, c, q, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_add_index0(c, b, a, n, rd_bytes, wr_bytes, fp_ops);
			stress_stream_triad_index0(a, b, c, q, n, rd_bytes, wr_bytes, fp_ops);
			t2 = stress_time_now();
		}
		break;
	}
	(*dt) += (t2 - t1);
}

static int OPTIMIZE3 TARGET_CLONES stress_stream_verify(
	stress_args_t *RESTRICT args,
	double *const RESTRICT old_checksum,
	const double *const RESTRICT a,
	const double *const RESTRICT b,
	const double *const RESTRICT c,
	const uint64_t n)
{
	double new_checksum;

	new_checksum = stress_stream_checksum_data(a, b, c, n);
	if ((*old_checksum > 0.0) && (fabs(new_checksum - *old_checksum) > 0.001)) {
		char new_str[32], old_str[32];

		stress_stream_checksum_to_hexstr(new_str, sizeof(new_str), new_checksum);
		stress_stream_checksum_to_hexstr(old_str, sizeof(old_str), *old_checksum);

		if (strcmp(old_str, new_str)) {
			pr_fail("%s: checksum failure, got 0x%s, expecting 0x%s\n",
				args->name, new_str, old_str);
			return EXIT_FAILURE;
		}
	} else {
		*old_checksum = new_checksum;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_stream()
 *	stress cache/memory/CPU with stream stressors
 */
static int stress_stream(stress_args_t *args)
{
	int rc = EXIT_FAILURE;
	double *a = MAP_FAILED, *b = MAP_FAILED, *c = MAP_FAILED;
	size_t *idx1 = MAP_FAILED, *idx2 = MAP_FAILED, *idx3 = MAP_FAILED;
	const double q = 3.0;
	double old_checksum = -1.0;
	double fp_ops = 0.0, dt;
	uint32_t w, z, stream_index = 0;
	uint64_t L3, sz, n, sz_idx;
	uint64_t stream_L3_size = DEFAULT_STREAM_L3_SIZE;
	uint32_t init_counter, init_counter_max;
	bool guess = false;
	bool stream_mlock = false;
	bool stream_prefetch = false;
#if defined(HAVE_NT_STORE_DOUBLE)
	const bool has_sse2 = stress_cpu_x86_has_sse2();
#else
	const bool has_sse2 = false;
#endif
	double rd_bytes = 0.0, wr_bytes = 0.0;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	stress_catch_sigill();

	(void)stress_get_setting("stream-mlock", &stream_mlock);
	(void)stress_get_setting("stream-prefetch", &stream_prefetch);

#if !defined(HAVE_PRAGMA_PREFETCH)
	if (stream_prefetch) {
		pr_inf("%s: prefetch not available, disabling option --stream-prefetch\n", args->name);
		stream_prefetch = false;
	}
#endif

	if (stress_get_setting("stream-l3-size", &stream_L3_size))
		L3 = stream_L3_size;
	else
		L3 = get_stream_L3_size(args);

	(void)stress_get_setting("stream-index", &stream_index);

	/* Have to take a hunch and badly guess size */
	if (!L3) {
		guess = true;
		L3 = (uint64_t)stress_get_processors_configured() * DEFAULT_STREAM_L3_SIZE;
	}

	if (stress_instance_zero(args)) {
		pr_inf("%s: stressor loosely based on a variant of the "
			"STREAM benchmark code\n", args->name);
		pr_inf("%s: do NOT submit any of these results "
			"to the STREAM benchmark results\n", args->name);
		if (guess) {
			pr_inf("%s: cannot determine CPU L3 cache size, "
				"defaulting to %" PRIu64 "K\n",
				args->name, L3 / 1024);
		} else {
			pr_inf("%s: Using cache size of %" PRIu64 "K\n",
				args->name, L3 / 1024);
		}
	}

	/* ..and shared amongst all the STREAM stressor instances */
	L3 /= args->instances;
	if (L3 < args->page_size)
		L3 = args->page_size;

	/*
	 *  Each array must be at least 4 x the
	 *  size of the L3 cache
	 */
	sz = (L3 * 4);
	n = sz / sizeof(*a);
	/*
	 *  n must be a multiple of the max unroll size (8)
	 */
	n = (n + 7) & ~(uint64_t)7;
	sz = n * sizeof(*a);
	sz_idx = n * sizeof(size_t);

	a = stress_stream_mmap(args, sz, stream_mlock);
	if (a == MAP_FAILED)
		goto err_unmap;
	b = stress_stream_mmap(args, sz, stream_mlock);
	if (b == MAP_FAILED)
		goto err_unmap;
	c = stress_stream_mmap(args, sz, stream_mlock);
	if (c == MAP_FAILED)
		goto err_unmap;

	switch (stream_index) {
	case 3:
		idx3 = stress_stream_mmap(args, sz_idx, stream_mlock);
		if (idx3 == MAP_FAILED)
			goto err_unmap;
		stress_stream_init_index(idx3, n);
		goto case_stream_index_2;
	case 2:
case_stream_index_2:
		idx2 = stress_stream_mmap(args, sz_idx, stream_mlock);
		if (idx2 == MAP_FAILED)
			goto err_unmap;
		stress_stream_init_index(idx2, n);
		goto case_stream_index_1;
	case 1:
case_stream_index_1:
		idx1 = stress_stream_mmap(args, sz_idx, stream_mlock);
		if (idx1 == MAP_FAILED)
			goto err_unmap;
		stress_stream_init_index(idx1, n);
		break;
	case 0:
	default:
		break;
	}

	stress_mwc_get_seed(&w, &z);

	init_counter = 0;
	init_counter_max = verify ? 1 : 64;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = EXIT_SUCCESS;
	dt = 0.0;
	do {
		if (init_counter == 0) {
			stress_mwc_set_seed(w, z);
			stress_stream_init_data(a, b, c, n);
		}
		init_counter++;
		if (init_counter >= init_counter_max)
			init_counter = 0;

		stress_stream_exercise(&dt, a, b, c, idx1, idx2, idx3,
				       &rd_bytes, &wr_bytes, &fp_ops,
				       q, n, stream_index, has_sse2, stream_prefetch);
		if (verify) {
			rc = stress_stream_verify(args, &old_checksum, a, b, c, n);
			if (rc != EXIT_SUCCESS)
				break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (dt >= 4.5) {
		const double mb_rd_rate = (rd_bytes / (double)MB) / dt;
		const double mb_wr_rate = (wr_bytes / (double)MB) / dt;
		const double fp_rate = (fp_ops / 1000000.0) / dt;

		pr_inf("%s: memory rate: %.2f MB read/sec, %.2f MB write/sec, %.2f double precision Mflop/sec"
			" (instance %" PRIu32 ")\n",
			args->name, mb_rd_rate, mb_wr_rate, fp_rate, args->instance);
		stress_metrics_set(args, 0, "MB per sec memory read rate",
			mb_rd_rate, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 1, "MB per sec memory write rate",
			mb_wr_rate, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 2, "Mflop per sec (double precision) compute rate",
			fp_rate, STRESS_METRIC_HARMONIC_MEAN);
	} else {
		if (stress_instance_zero(args))
			pr_inf("%s: run duration too short to reliably determine memory rate\n", args->name);
	}

err_unmap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (idx3 != MAP_FAILED)
		(void)munmap((void *)idx3, sz_idx);
	if (idx2 != MAP_FAILED)
		(void)munmap((void *)idx2, sz_idx);
	if (idx1 != MAP_FAILED)
		(void)munmap((void *)idx1, sz_idx);
	if (c != MAP_FAILED)
		(void)munmap((void *)c, sz);
	if (b != MAP_FAILED)
		(void)munmap((void *)b, sz);
	if (a != MAP_FAILED)
		(void)munmap((void *)a, sz);
	return rc;
}

static const char *stress_stream_madvise(const size_t i)
{
	return (i < SIZEOF_ARRAY(stream_madvise_info)) ? stream_madvise_info[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_stream_index,    "stream-index",    TYPE_ID_UINT32, 0, 3, NULL },
	{ OPT_stream_l3_size,  "stream-l3-size",  TYPE_ID_UINT64_BYTES_VM, MIN_STREAM_L3_SIZE, MAX_STREAM_L3_SIZE, NULL },
	{ OPT_stream_madvise,  "stream-madvise",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_stream_madvise },
	{ OPT_stream_mlock,    "stream-mlock",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_stream_prefetch, "stream-prefetch", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_stream_info = {
	.stressor = stress_stream,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
