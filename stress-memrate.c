/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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

typedef uint64_t (*memrate_func_t)(void *start, void *end, uint64_t rd_mbs, uint64_t wr_mbs);

typedef struct {
	const char *name;
	memrate_func_t	func;
	double		duration;
	double		mbytes;
} memrate_info_t;

void stress_set_memrate_bytes(const char *opt)
{
	uint64_t memrate_bytes;

	memrate_bytes = get_uint64_byte(opt);
	check_range_bytes("memrate-bytes", memrate_bytes,
		MIN_MEMRATE_BYTES, MAX_MEMRATE_BYTES);
	set_setting("memrate-bytes", TYPE_ID_UINT64, &memrate_bytes);
}

void stress_set_memrate_rd_mbs(const char *opt)
{
	uint64_t memrate_rd_mbs;

	memrate_rd_mbs = get_uint64(opt);
	check_range_bytes("memrate-rd-mbs", memrate_rd_mbs,
		1, 1000000);
	set_setting("memrate-rd-mbs", TYPE_ID_UINT64, &memrate_rd_mbs);
}

void stress_set_memrate_wr_mbs(const char *opt)
{
	uint64_t memrate_wr_mbs;

	memrate_wr_mbs = get_uint64(opt);
	check_range_bytes("memrate-wr-mbs", memrate_wr_mbs,
		1, 1000000);
	set_setting("memrate-wr-mbs", TYPE_ID_UINT64, &memrate_wr_mbs);
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
	t1 = time_now();					\
	for (ptr = start; ptr < (uint##size##_t *)end;) {	\
		double t2, remainder;				\
		int32_t i;					\
								\
		if (!g_keep_stressing_flag)			\
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
		t2 = time_now();				\
		total_dur += dur;				\
		remainder = total_dur - (t2 - t1);		\
								\
		if (remainder >= 0.0) {				\
			struct timespec t;			\
								\
			t.tv_sec = (time_t)remainder;		\
			t.tv_nsec = (remainder -		\
				(long)remainder) * 1000000000;	\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	return ((void *)ptr - start) / MB;			\
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
	t1 = time_now();					\
	for (ptr = start; ptr < (uint##size##_t *)end;) {	\
		double t2, remainder;				\
		int32_t i;					\
								\
		if (!g_keep_stressing_flag)			\
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
		t2 = time_now();				\
		total_dur += dur;				\
		remainder = total_dur - (t2 - t1);		\
								\
		if (remainder >= 0.0) {				\
			struct timespec t;			\
								\
			t.tv_sec = (time_t)remainder;		\
			t.tv_nsec = (remainder -		\
				(long)remainder) * 1000000000;	\
			(void)nanosleep(&t, NULL);		\
		}						\
	}							\
	return ((void *)ptr - start) / MB;			\
}

STRESS_MEMRATE_WRITE(64)
STRESS_MEMRATE_WRITE(32)
STRESS_MEMRATE_WRITE(16)
STRESS_MEMRATE_WRITE(8)

static memrate_info_t memrate_info[] = {
	{ "write64",	stress_memrate_write64,	0.0, 0 },
	{ "read64",	stress_memrate_read64,  0.0, 0 },
	{ "write32",	stress_memrate_write32,	0.0, 0 },
	{ "read32",	stress_memrate_read32,  0.0, 0 },
	{ "write16",	stress_memrate_write16,	0.0, 0 },
	{ "read16",	stress_memrate_read16,  0.0, 0 },
	{ "write8",	stress_memrate_write8,	0.0, 0 },
	{ "read8",	stress_memrate_read8,   0.0, 0 },
};

static void OPTIMIZE3 stress_memrate_init_data(
	void *start,
	void *end)
{
	register volatile uint32_t *ptr;

	for (ptr = start; ptr < (uint32_t *)end; ptr++)
		*ptr = mwc32();
}

static inline void *stress_memrate_mmap(const args_t *args, uint64_t sz)
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

		(void)get_setting("memrate-madvise", &advice);

		ret = madvise(ptr, sz, advice);
		(void)ret;
#endif
	}
	return ptr;
}

/*
 *  stress_memrate()
 *	stress cache/memory/CPU with memrate stressors
 */
int stress_memrate(const args_t *args)
{
	int rc = EXIT_FAILURE;
	void *buffer, *buffer_end;
	uint64_t memrate_bytes  = DEFAULT_MEMRATE_BYTES;
	uint64_t memrate_rd_mbs = ~0;
	uint64_t memrate_wr_mbs = ~0;
	size_t i;

	(void)get_setting("memrate-bytes", &memrate_bytes);
	(void)get_setting("memrate-rd-mbs", &memrate_rd_mbs);
	(void)get_setting("memrate-wr-mbs", &memrate_wr_mbs);

	memrate_bytes = (memrate_bytes + 63) & ~(63);

	buffer = stress_memrate_mmap(args, memrate_bytes);
	buffer_end = (uint8_t *)buffer + memrate_bytes;

	stress_memrate_init_data(buffer, buffer_end);

	do {
		for (i = 0; keep_stressing() &&
		     (i < SIZEOF_ARRAY(memrate_info)); i++) {
			double t1, t2;
			memrate_info_t *info = &memrate_info[i];

			t1 = time_now();
			info->mbytes += info->func(buffer, buffer_end,
				memrate_rd_mbs, memrate_wr_mbs);
			t2 = time_now();
			info->duration += (t2 - t1);

			if (!keep_stressing())
				break;
		}

		inc_counter(args);
	} while (keep_stressing());

	for (i = 0; i < SIZEOF_ARRAY(memrate_info); i++) {
		memrate_info_t *info = &memrate_info[i];

		if (info->duration > 0.001)
			pr_inf("%s: %7.7s: %.2f MB/sec\n",
				args->name, info->name,
				info->mbytes / info->duration);
		else
			pr_inf("%s: %7.7s: interrupted early\n",
				args->name, info->name);
	}

	rc = EXIT_SUCCESS;
	(void)munmap(buffer, memrate_bytes);

	return rc;
}
