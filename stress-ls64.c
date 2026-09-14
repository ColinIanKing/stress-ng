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
    defined(__ARM_FEATURE_LS64)

#include <arm_acle.h>
#include <sys/auxv.h>

#define LS64_CHUNKS		(256)	/* 256 x 64 byte chunks = 16KB */
#define LS64_CHUNK_BYTES	(64)

typedef union {
	data512_t		d512;	/* the ls64 load/store 64 byte type */
	uint64_t		u64[LS64_CHUNK_BYTES / sizeof(uint64_t)];
} stress_ls64_chunk_t;

typedef struct {
	stress_ls64_chunk_t	chunks[LS64_CHUNKS] ALIGN64;
	uint64_t		golden[LS64_CHUNKS][LS64_CHUNK_BYTES / sizeof(uint64_t)];
} stress_ls64_data_t;

static const stress_help_t help[] = {
	{ NULL,	"ls64 N",	"start N workers exercising 64 byte ld64b/st64b atomic load/stores" },
	{ NULL,	"ls64-ops N",	"stop after N ls64 bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  ls64_supported()
 *	LD64B/ST64B instructions require both build-time ls64 code
 *	generation (implied by __ARM_FEATURE_LS64 being defined for
 *	this translation unit) and run-time hardware support.
 *	The ls64 feature is advertised in AT_HWCAP2 (HWCAP2_LS64)
 *	on older kernels and in AT_HWCAP3 (HWCAP3_LS64, bit 0) on
 *	kernels that have exhausted the HWCAP2 bits, so check both.
 *	AT_HWCAP3 is 29 per the uapi auxvec.h; glibc may not define
 *	it yet, so use the raw constant with a guard.
 */
#ifndef AT_HWCAP3
#define AT_HWCAP3		(29)
#endif
#define STRESS_HWCAP3_LS64	(1UL << 0)	/* HWCAP3_LS64 */
#define STRESS_HWCAP2_LS64	(1UL << 15)	/* HWCAP2_LS64 */

static int ls64_supported(const char *name)
{
	const unsigned long hwcap2 = getauxval(AT_HWCAP2);
	const unsigned long hwcap3 = getauxval(AT_HWCAP3);

	if (!(hwcap2 & STRESS_HWCAP2_LS64) &&
	    !(hwcap3 & STRESS_HWCAP3_LS64)) {
		pr_inf_skip("%s: stressor will be skipped, "
			"CPU does not support the ls64 (64 byte load/store) extension\n",
			name);
		return -1;
	}
	return 0;
}

/*
 *  ls64_roundtrip()
 *	read-modify-write pass over all chunks using the 64 byte
 *	atomic load/store instructions; each 64 bit lane is rotated
 *	by a per-chunk amount so every bit moves each round
 */
static void ls64_roundtrip(stress_ls64_data_t *data)
{
	size_t c;

	for (c = 0; c < LS64_CHUNKS; c++) {
		stress_ls64_chunk_t chunk;
		size_t i;

		chunk.d512 = __arm_ld64b(&data->chunks[c]);
		for (i = 0; i < SIZEOF_ARRAY(chunk.u64); i++)
			chunk.u64[i] = (chunk.u64[i] << 7) | (chunk.u64[i] >> 57);
		__arm_st64b(&data->chunks[c], chunk.d512);
	}
}

/*
 *  golden_roundtrip()
 *	scalar reference of the same transformation on the golden
 *	copy of the data
 */
static void golden_roundtrip(stress_ls64_data_t *data)
{
	size_t c, i;

	for (c = 0; c < LS64_CHUNKS; c++) {
		for (i = 0; i < LS64_CHUNK_BYTES / sizeof(uint64_t); i++) {
			const uint64_t v = data->golden[c][i];

			data->golden[c][i] = (v << 7) | (v >> 57);
		}
	}
}

static void ls64_verify_fail(
	const char *name,
	const size_t chunk,
	const size_t idx,
	const uint64_t expected,
	const uint64_t actual)
{
	const uint64_t xor = expected ^ actual;

	pr_fail("%s: ld64b/st64b roundtrip mismatch at chunk %zu lane %zu: "
		"expected 0x%16.16" PRIx64 ", actual 0x%16.16" PRIx64 ", "
		"%u bit(s) flipped (xor 0x%16.16" PRIx64 ")\n",
		name, chunk, idx, expected, actual,
		stress_bitops_popcount64(xor), xor);
}

static int stress_ls64(stress_args_t *args)
{
	stress_ls64_data_t *data;
	const size_t sz = sizeof(*data);
	size_t c, i;
	int rc = EXIT_SUCCESS;

	data = (stress_ls64_data_t *)stress_mmap_populate(NULL, sz,
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

	/*  Seed data  */
	for (c = 0; c < LS64_CHUNKS; c++) {
		for (i = 0; i < LS64_CHUNK_BYTES / sizeof(uint64_t); i++) {
			const uint64_t v = stress_mwc64();

			data->chunks[c].u64[i] = v;
			data->golden[c][i] = v;
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		size_t n;

		for (n = 0; n < 16; n++)
			ls64_roundtrip(data);
		golden_roundtrip(data);
		/*
		 *  golden does one rotation per round; each ls64_roundtrip
		 *  call is one rotation too, so compare after 16 rounds
		 *  by applying the scalar rotation 16 times
		 */
		for (n = 0; n < 15; n++)
			golden_roundtrip(data);

		for (c = 0; c < LS64_CHUNKS; c++) {
			for (i = 0; i < LS64_CHUNK_BYTES / sizeof(uint64_t); i++) {
				if (UNLIKELY(data->chunks[c].u64[i] != data->golden[c][i])) {
					ls64_verify_fail(args->name, c, i,
						data->golden[c][i], data->chunks[c].u64[i]);
					rc = EXIT_FAILURE;
				}
			}
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	(void)munmap(data, sz);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("64-byte-atomic-load-store"),
	STRESS_EX_FEATURE("lsu-64b-datapath"),

	STRESS_EX_END,
};

const stressor_info_t stress_ls64_info = {
	.stressor = stress_ls64,
	.classifier = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.supported = ls64_supported,
	.help = help,
	.exercises = exercises,
};

#else

const stressor_info_t stress_ls64_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "built for non-aarch64 target or compiler without the ls64 (64 byte atomic load/store) extension"
};

#endif
