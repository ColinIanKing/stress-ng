/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright 2021 Colin Ian King
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

static const stress_help_t help[] = {
	{ NULL,	"memrate N",		"start N workers exercised memory read/writes" },
	{ NULL,	"memrate-ops N",	"stop after N memrate bogo operations" },
	{ NULL,	"memrate-bytes N",	"size of memory buffer being exercised" },
	{ NULL,	"memrate-rd-mbs N",	"read rate from buffer in megabytes per second" },
	{ NULL,	"memrate-wr-mbs N",	"write rate to buffer in megabytes per second" },
	{ NULL,	NULL,			NULL }
};

typedef uint64_t (*stress_memrate_func_t)(void *start, void *end, uint64_t rd_mbs, uint64_t wr_mbs, bool *valid);

typedef struct {
	const char 	*name;
	stress_memrate_func_t	func;
} stress_memrate_info_t;

typedef struct {
	double		duration;
	double		kbytes;
	bool		valid;
} stress_memrate_stats_t;

typedef struct {
	stress_memrate_stats_t *stats;
	uint64_t memrate_bytes;
	uint64_t memrate_rd_mbs;
	uint64_t memrate_wr_mbs;
} stress_memrate_context_t;

static int stress_set_memrate_bytes(const char *opt)
{
	uint64_t memrate_bytes;

	memrate_bytes = stress_get_uint64_byte(opt);
	stress_check_range_bytes("memrate-bytes", memrate_bytes,
		MIN_MEMRATE_BYTES, MAX_MEMRATE_BYTES);
	return stress_set_setting("memrate-bytes", TYPE_ID_UINT64, &memrate_bytes);
}

static int stress_set_memrate_rd_mbs(const char *opt)
{
	uint64_t memrate_rd_mbs;

	memrate_rd_mbs = stress_get_uint64(opt);
	stress_check_range_bytes("memrate-rd-mbs", memrate_rd_mbs,
		1, 1000000);
	return stress_set_setting("memrate-rd-mbs", TYPE_ID_UINT64, &memrate_rd_mbs);
}

static int stress_set_memrate_wr_mbs(const char *opt)
{
	uint64_t memrate_wr_mbs;

	memrate_wr_mbs = stress_get_uint64(opt);
	stress_check_range_bytes("memrate-wr-mbs", memrate_wr_mbs,
		1, 1000000);
	return stress_set_setting("memrate-wr-mbs", TYPE_ID_UINT64, &memrate_wr_mbs);
}

#define SINGLE_ARG(...) __VA_ARGS__

#define STRESS_MEMRATE_READ(size, type)				\
static uint64_t stress_memrate_read##size(			\
	void *start,						\
	void *end,						\
	uint64_t rd_mbs,					\
	uint64_t wr_mbs,					\
	bool *valid)						\
{								\
	register volatile type *ptr;				\
	double t1;						\
	const double dur = 1.0 / (double)rd_mbs;		\
	double total_dur = 0.0;					\
								\
	(void)wr_mbs;						\
								\
	t1 = stress_time_now();					\
	for (ptr = start; ptr < (type *)end;) {			\
		double t2, dur_remainder;			\
		uint32_t i;					\
								\
		if (!keep_stressing_flag())			\
			break;					\
		for (i = 0; (i < (uint32_t)MB) &&		\
		     (ptr < (type *)end);			\
		     ptr += 8, i += size) {			\
			(void)(ptr[0]);				\
			(void)(ptr[1]);				\
			(void)(ptr[2]);				\
			(void)(ptr[3]);				\
			(void)(ptr[4]);				\
			(void)(ptr[5]);				\
			(void)(ptr[6]);				\
			(void)(ptr[7]);				\
		}						\
		t2 = stress_time_now();				\
		total_dur += dur;				\
		dur_remainder = total_dur - (t2 - t1);		\
								\
		if (dur_remainder >= 0.0) {			\
			struct timespec t;			\
			time_t sec = (time_t)dur_remainder;	\
								\
			t.tv_sec = sec;				\
			t.tv_nsec = (long)((dur_remainder -	\
				(double)sec) *			\
				STRESS_NANOSECOND);		\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	*valid = true;						\
	return ((uintptr_t)ptr - (uintptr_t)start) / KB;	\
}

#if defined(HAVE_INT128_T)
STRESS_MEMRATE_READ(128, __uint128_t)
#endif
STRESS_MEMRATE_READ(64, uint64_t)
STRESS_MEMRATE_READ(32, uint32_t)
STRESS_MEMRATE_READ(16, uint16_t)
STRESS_MEMRATE_READ(8, uint8_t)

#define STRESS_MEMRATE_WRITE(size, type)			\
static uint64_t stress_memrate_write##size(			\
	void *start,						\
	void *end,						\
	uint64_t rd_mbs,					\
	uint64_t wr_mbs,					\
	bool *valid)						\
{								\
	register volatile type *ptr;				\
	double t1;						\
	const double dur = 1.0 / (double)wr_mbs;		\
	double total_dur = 0.0;					\
								\
	(void)rd_mbs;						\
								\
	t1 = stress_time_now();					\
	for (ptr = start; ptr < (type *)end;) {			\
		double t2, dur_remainder;			\
		uint32_t i;					\
								\
		if (!keep_stressing_flag())			\
			break;					\
		for (i = 0; (i < (uint32_t)MB) &&		\
		     (ptr < (type *)end);			\
		     ptr += 8, i += size) {			\
			ptr[0] = (uint8_t)i;			\
			ptr[1] = (uint8_t)i;			\
			ptr[2] = (uint8_t)i;			\
			ptr[3] = (uint8_t)i;			\
			ptr[4] = (uint8_t)i;			\
			ptr[5] = (uint8_t)i;			\
			ptr[6] = (uint8_t)i;			\
			ptr[7] = (uint8_t)i;			\
		}						\
		t2 = stress_time_now();				\
		total_dur += dur;				\
		dur_remainder = total_dur - (t2 - t1);		\
								\
		if (dur_remainder >= 0.0) {			\
			struct timespec t;			\
			time_t sec = (time_t)dur_remainder;	\
								\
			t.tv_sec = sec;				\
			t.tv_nsec = (long)((dur_remainder -	\
				(double)sec) * 			\
				STRESS_NANOSECOND);		\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	*valid = true;						\
	return ((uintptr_t)ptr - (uintptr_t)start) / KB;	\
}

/*
 *
 * See https://akkadia.org/drepper/cpumemory.pdf - section 6.1
 *  non-temporal writes using movntdq. Data is not going to be
 *  read, so no need to cache. Write directly to memory.
 */

#define STRESS_MEMRATE_WRITE_NT(size, type, movtype, op, init)	\
static uint64_t stress_memrate_write_nt##size(			\
	void *start,						\
	void *end,						\
	uint64_t rd_mbs,					\
	uint64_t wr_mbs,					\
	bool *valid)						\
{								\
	register type *ptr;					\
	double t1;						\
	const double dur = 1.0 / (double)wr_mbs;		\
	double total_dur = 0.0;					\
								\
	(void)rd_mbs;						\
								\
	if (!__builtin_cpu_supports("sse") && 			\
            !__builtin_cpu_supports("sse2")) {			\
		*valid = false;					\
		return 0;					\
	}							\
								\
	t1 = stress_time_now();					\
	for (ptr = start; ptr < (type *)end;) {			\
		double t2, dur_remainder;			\
		uint32_t i;					\
								\
		if (!keep_stressing_flag())			\
			break;					\
		for (i = 0; (i < (uint32_t)MB) &&		\
		     (ptr < (type *)end);			\
		     ptr += 8, i += size) {			\
			movtype v = (movtype)init;		\
			movtype *vptr = (movtype *)ptr;		\
								\
			op(&vptr[0], v);			\
			op(&vptr[1], v);			\
			op(&vptr[2], v);			\
			op(&vptr[3], v);			\
			op(&vptr[4], v);			\
			op(&vptr[5], v);			\
			op(&vptr[6], v);			\
			op(&vptr[7], v);			\
		}						\
		t2 = stress_time_now();				\
		total_dur += dur;				\
		dur_remainder = total_dur - (t2 - t1);		\
								\
		if (dur_remainder >= 0.0) {			\
			struct timespec t;			\
			time_t sec = (time_t)dur_remainder;	\
								\
			t.tv_sec = sec;				\
			t.tv_nsec = (long)((dur_remainder -	\
				(double)sec) * 			\
				STRESS_NANOSECOND);		\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	*valid = true;						\
	return ((uintptr_t)ptr - (uintptr_t)start) / KB;	\
}

#define __BUILTIN_NONTEMPORAL_STORE(a, b)	__builtin_nontemporal_store(b, a)

#if defined(HAVE_INT128_T) &&		\
    defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_NONTEMPORAL_STORE)
/* Clang non-temporal stores */
STRESS_MEMRATE_WRITE_NT(128, __uint128_t, __uint128_t, __BUILTIN_NONTEMPORAL_STORE, i)
#define HAVE_WRITE128NT
#elif defined(HAVE_XMMINTRIN_H) &&	\
    defined(HAVE_INT128_T) &&		\
    defined(HAVE_V2DI) && 		\
    defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_IA32_MOVNTDQ)
/* gcc x86 non-temporal stores */
STRESS_MEMRATE_WRITE_NT(128, __uint128_t, __v2di, __builtin_ia32_movntdq, SINGLE_ARG({ 0, i }))
#define HAVE_WRITE128NT
#endif

#if defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_NONTEMPORAL_STORE)
/* Clang non-temporal stores */
STRESS_MEMRATE_WRITE_NT(64, uint64_t, uint64_t, __BUILTIN_NONTEMPORAL_STORE, i)
#define HAVE_WRITE64NT
#elif defined(HAVE_XMMINTRIN_H) &&	\
    defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_IA32_MOVNTI64)
STRESS_MEMRATE_WRITE_NT(64, uint64_t, long long int, __builtin_ia32_movnti64, i)
#define HAVE_WRITE64NT
#endif

#if defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_NONTEMPORAL_STORE)
/* Clang non-temporal stores */
STRESS_MEMRATE_WRITE_NT(32, uint32_t, uint32_t, __BUILTIN_NONTEMPORAL_STORE, i)
#define HAVE_WRITE32NT
#elif defined(HAVE_XMMINTRIN_H) &&	\
    defined(HAVE_BUILTIN_SUPPORTS) &&	\
    defined(HAVE_BUILTIN_IA32_MOVNTI)
STRESS_MEMRATE_WRITE_NT(32, uint32_t, int, __builtin_ia32_movnti, i)
#define HAVE_WRITE32NT
#endif

#if defined(HAVE_INT128_T)
STRESS_MEMRATE_WRITE(128, __uint128_t)
#endif
STRESS_MEMRATE_WRITE(64, uint64_t)
STRESS_MEMRATE_WRITE(32, uint32_t)
STRESS_MEMRATE_WRITE(16, uint16_t)
STRESS_MEMRATE_WRITE(8, uint8_t)

static stress_memrate_info_t memrate_info[] = {
#if defined(HAVE_WRITE128NT)
	{ "write128nt",	stress_memrate_write_nt128 },
#endif
#if defined(HAVE_WRITE64NT)
	{ "write64nt",	stress_memrate_write_nt64 },
#endif
#if defined(HAVE_WRITE32NT)
	{ "write32nt",	stress_memrate_write_nt32 },
#endif

#if defined(HAVE_INT128_T)
	{ "write128",	stress_memrate_write128 },
#endif
	{ "write64",	stress_memrate_write64 },
	{ "write32",	stress_memrate_write32 },
	{ "write16",	stress_memrate_write16 },
	{ "write8",	stress_memrate_write8 },

#if defined(HAVE_INT128_T)
	{ "read128",	stress_memrate_read128 },
#endif
	{ "read64",	stress_memrate_read64 },
	{ "read32",	stress_memrate_read32 },
	{ "read16",	stress_memrate_read16 },
	{ "read8",	stress_memrate_read8 }
};

static const size_t memrate_items = SIZEOF_ARRAY(memrate_info);

static void OPTIMIZE3 stress_memrate_init_data(
	void *start,
	void *end)
{
	register volatile uint32_t *ptr;

	for (ptr = start; ptr < (uint32_t *)end; ptr++)
		*ptr = stress_mwc32();
}

static inline void *stress_memrate_mmap(const stress_args_t *args, uint64_t sz)
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
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_HUGEPAGE)
		int ret, advice = MADV_NORMAL;

		ret = madvise(ptr, sz, advice);
		(void)ret;
#endif
	}
	return ptr;
}

static int stress_memrate_child(const stress_args_t *args, void *ctxt)
{
	const stress_memrate_context_t *context = (stress_memrate_context_t *)ctxt;
	void *buffer, *buffer_end;

	buffer = stress_memrate_mmap(args, context->memrate_bytes);
	if (buffer == MAP_FAILED)
		return EXIT_NO_RESOURCE;

	buffer_end = (uint8_t *)buffer + context->memrate_bytes;
	stress_memrate_init_data(buffer, buffer_end);

	do {
		size_t i;

		for (i = 0; keep_stressing(args) && (i < memrate_items); i++) {
			double t1, t2;
			uint64_t kbytes;
			stress_memrate_info_t *info = &memrate_info[i];
			bool valid = false;

			t1 = stress_time_now();
			kbytes = info->func(buffer, buffer_end,
					    context->memrate_rd_mbs,
					    context->memrate_wr_mbs, &valid);
			context->stats[i].kbytes += (double)kbytes;
			t2 = stress_time_now();
			context->stats[i].duration += (t2 - t1);
			context->stats[i].valid = valid;

			if (!keep_stressing(args))
				break;
		}

		inc_counter(args);
	} while (keep_stressing(args));

	(void)munmap(buffer, context->memrate_bytes);
	return EXIT_SUCCESS;
}

/*
 *  stress_memrate()
 *	stress cache/memory/CPU with memrate stressors
 */
static int stress_memrate(const stress_args_t *args)
{
	int rc;
	size_t i, stats_size;
	bool lock = false;
	stress_memrate_context_t context;

	context.memrate_bytes = DEFAULT_MEMRATE_BYTES;
	context.memrate_rd_mbs = ~0ULL;
	context.memrate_wr_mbs = ~0ULL;

	(void)stress_get_setting("memrate-bytes", &context.memrate_bytes);
	(void)stress_get_setting("memrate-rd-mbs", &context.memrate_rd_mbs);
	(void)stress_get_setting("memrate-wr-mbs", &context.memrate_wr_mbs);

	stats_size = memrate_items * sizeof(*context.stats);
	stats_size = (stats_size + args->page_size - 1) & ~(args->page_size - 1);

	context.stats = (stress_memrate_stats_t *)mmap(NULL, stats_size,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (context.stats == MAP_FAILED)
		return EXIT_NO_RESOURCE;
	for (i = 0; i < memrate_items; i++) {
		context.stats[i].duration = 0.0;
		context.stats[i].kbytes = 0.0;
		context.stats[i].valid = false;
	}

	context.memrate_bytes = (context.memrate_bytes + 63) & ~(63ULL);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, &context, stress_memrate_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	pr_lock(&lock);
	for (i = 0; i < memrate_items; i++) {
		if (!context.stats[i].valid)
			continue;
		if (context.stats[i].duration > 0.001) {
			char tmp[32];
			const double rate = context.stats[i].kbytes / (context.stats[i].duration * KB);

			pr_inf_lock(&lock, "%s: %10.10s: %12.2f MB/sec\n",
				args->name, memrate_info[i].name, rate);

			(void)snprintf(tmp, sizeof(tmp), "%s MB/sec", memrate_info[i].name);
			stress_misc_stats_set(args->misc_stats, i, tmp, rate);
		} else {
			pr_inf_lock(&lock, "%s: %10.10s: interrupted early\n",
				args->name, memrate_info[i].name);
		}
	}
	pr_unlock(&lock);

	(void)munmap((void *)context.stats, stats_size);

	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_memrate_bytes,	stress_set_memrate_bytes },
	{ OPT_memrate_rd_mbs,	stress_set_memrate_rd_mbs },
	{ OPT_memrate_wr_mbs,	stress_set_memrate_wr_mbs },
	{ 0,			NULL }
};

stressor_info_t stress_memrate_info = {
	.stressor = stress_memrate,
	.class = CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
