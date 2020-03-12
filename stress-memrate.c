/*
 * Copyright (C) 2016-2020 Canonical, Ltd.
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
 */
#include "stress-ng.h"

#define STRESS_NANOSEC		(1000000000)

static const stress_help_t help[] = {
	{ NULL,	"memrate N",		"start N workers exercised memory read/writes" },
	{ NULL,	"memrate-ops N",	"stop after N memrate bogo operations" },
	{ NULL,	"memrate-bytes N",	"size of memory buffer being exercised" },
	{ NULL,	"memrate-rd-mbs N",	"read rate from buffer in megabytes per second" },
	{ NULL,	"memrate-wr-mbs N",	"write rate to buffer in megabytes per second" },
	{ NULL,	NULL,			NULL }
};

typedef uint64_t (*stress_memrate_func_t)(void *start, void *end, uint64_t rd_mbs, uint64_t wr_mbs);

typedef struct {
	const char 	*name;
	stress_memrate_func_t	func;
} stress_memrate_info_t;

typedef struct {
	double		duration;
	double		kbytes;
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

#define STRESS_MEMRATE_READ(size)				\
static uint64_t stress_memrate_read##size(			\
	void *start,						\
	void *end,						\
	uint64_t rd_mbs,					\
	uint64_t wr_mbs)					\
{								\
	register volatile uint##size##_t *ptr;			\
	double t1;						\
	const double dur = 1.0 / (double)rd_mbs;		\
	double total_dur = 0.0;					\
								\
	(void)wr_mbs;						\
								\
	t1 = stress_time_now();					\
	for (ptr = start; ptr < (uint##size##_t *)end;) {	\
		double t2, dur_remainder;			\
		int32_t i;					\
								\
		if (!keep_stressing_flag())			\
			break;					\
		for (i = 0; (i < (int32_t)MB) &&		\
		     (ptr < (uint##size##_t *)end);		\
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
								\
			t.tv_sec = (time_t)dur_remainder;	\
			t.tv_nsec = (dur_remainder -		\
				(long)dur_remainder) *		\
				STRESS_NANOSEC;			\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	return ((volatile void *)ptr - start) / KB;		\
}

STRESS_MEMRATE_READ(64)
STRESS_MEMRATE_READ(32)
STRESS_MEMRATE_READ(16)
STRESS_MEMRATE_READ(8)

#define STRESS_MEMRATE_WRITE(size)				\
static uint64_t stress_memrate_write##size(			\
	void *start,						\
	void *end,						\
	uint64_t rd_mbs,					\
	uint64_t wr_mbs)					\
{								\
	register volatile uint##size##_t *ptr;			\
	double t1;						\
	const double dur = 1.0 / (double)wr_mbs;		\
	double total_dur = 0.0;					\
								\
	(void)rd_mbs;						\
								\
	t1 = stress_time_now();					\
	for (ptr = start; ptr < (uint##size##_t *)end;) {	\
		double t2, dur_remainder;			\
		int32_t i;					\
								\
		if (!keep_stressing_flag())			\
			break;					\
		for (i = 0; (i < (int32_t)MB) &&		\
		     (ptr < (uint##size##_t *)end);		\
		     ptr += 8, i += size) {			\
			ptr[0] = i;				\
			ptr[1] = i;				\
			ptr[2] = i;				\
			ptr[3] = i;				\
			ptr[4] = i;				\
			ptr[5] = i;				\
			ptr[6] = i;				\
			ptr[7] = i;				\
		}						\
		t2 = stress_time_now();				\
		total_dur += dur;				\
		dur_remainder = total_dur - (t2 - t1);		\
								\
		if (dur_remainder >= 0.0) {			\
			struct timespec t;			\
								\
			t.tv_sec = (time_t)dur_remainder;	\
			t.tv_nsec = (dur_remainder -		\
				(long)dur_remainder) * 		\
				STRESS_NANOSEC;			\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	return ((volatile void *)ptr - start) / KB;		\
}

STRESS_MEMRATE_WRITE(64)
STRESS_MEMRATE_WRITE(32)
STRESS_MEMRATE_WRITE(16)
STRESS_MEMRATE_WRITE(8)

static stress_memrate_info_t memrate_info[] = {
	{ "write64",	stress_memrate_write64 },
	{ "read64",	stress_memrate_read64 },
	{ "write32",	stress_memrate_write32 },
	{ "read32",	stress_memrate_read32 },
	{ "write16",	stress_memrate_write16 },
	{ "read16",	stress_memrate_read16 },
	{ "write8",	stress_memrate_write8 },
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
#if defined(HAVE_MADVISE)
		int ret, advice = MADV_NORMAL;

		(void)stress_get_setting("memrate-madvise", &advice);

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

		for (i = 0; keep_stressing() && (i < memrate_items); i++) {
			double t1, t2;
			stress_memrate_info_t *info = &memrate_info[i];

			t1 = stress_time_now();
			context->stats[i].kbytes += info->func(buffer, buffer_end,
				context->memrate_rd_mbs, context->memrate_wr_mbs);
			t2 = stress_time_now();
			context->stats[i].duration += (t2 - t1);

			if (!keep_stressing())
				break;
		}

		inc_counter(args);
	} while (keep_stressing());

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
	context.memrate_rd_mbs = ~0;
	context.memrate_wr_mbs = ~0;

	(void)stress_get_setting("memrate-bytes", &context.memrate_bytes);
	(void)stress_get_setting("memrate-rd-mbs", &context.memrate_rd_mbs);
	(void)stress_get_setting("memrate-wr-mbs", &context.memrate_wr_mbs);

	stats_size = memrate_items * sizeof(stress_memrate_stats_t);
	stats_size = (stats_size + args->page_size - 1) & ~(args->page_size - 1);

	context.stats = (stress_memrate_stats_t *)mmap(NULL, stats_size,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (context.stats == MAP_FAILED)
		return EXIT_NO_RESOURCE;
	for (i = 0; i < memrate_items; i++) {
		context.stats[i].duration = 0.0;
		context.stats[i].kbytes = 0.0;
	}

	context.memrate_bytes = (context.memrate_bytes + 63) & ~(63);

	rc = stress_oomable_child(args, &context, stress_memrate_child, STRESS_OOMABLE_NORMAL);

	pr_lock(&lock);
	for (i = 0; i < memrate_items; i++) {
		if (context.stats[i].duration > 0.001)
			pr_inf_lock(&lock, "%s: %7.7s: %.2f MB/sec\n",
				args->name, memrate_info[i].name,
				context.stats[i].kbytes / (context.stats[i].duration * KB));
		else
			pr_inf_lock(&lock, "%s: %7.7s: interrupted early\n",
				args->name, memrate_info[i].name);
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
