/*
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
 */
#include "stress-ng.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-hash.h"

#include <math.h>

#if defined(HAVE_XXHASH_H)
#include <xxhash.h>
#endif

#define STRESS_HASH_N_BUCKETS	(256)
#define STRESS_HASH_N_KEYS	(128)

typedef struct {
	double		duration;
	double		chi_squared;
	uint64_t	total;
} stress_hash_stats_t;

typedef struct {
	uint64_t	buckets[STRESS_HASH_N_BUCKETS] ALIGNED(32);
	char 		buffer[STRESS_HASH_N_KEYS + 64] ALIGNED(32);
} stress_bucket_t;

struct stress_hash_method_info;
typedef struct stress_hash_method_info stress_hash_method_info_t;

typedef uint32_t (*stress_hash_func)(const char *str, const size_t len);
typedef int (*stress_method_func)(const char *name, const stress_hash_method_info_t * hmi,
				  stress_hash_stats_t *stats, stress_bucket_t *bucket);

struct stress_hash_method_info {
	const char		*name;	/* human readable form of stressor */
	const stress_method_func	func;	/* the hash method function */
};

static const stress_help_t help[] = {
	{ NULL,  "hash N",		"start N workers that exercise various hash functions" },
	{ NULL,  "hash-method M",	"specify stress hash method M, default is all" },
	{ NULL,  "hash-ops N",		"stop after N hash bogo operations" },
	{ NULL,	 NULL,			NULL }
};

/*
 *  stress_hash_generic()
 *	stress test generic string hash function
 */
static int OPTIMIZE3 stress_hash_generic(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket,
	const stress_hash_func hash_func,
	const uint32_t le_result,
	const uint32_t be_result)
{
	double sum = 0.0, n, m, divisor;
	uint32_t i_sum = 0;
	size_t i;
	const uint32_t result = stress_little_endian() ? le_result: be_result;
	double t1, t2;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (verify)
		stress_mwc_default_seed();

	(void)shim_memset(bucket->buckets, 0, sizeof(bucket->buckets));

	stress_uint8rnd4((uint8_t *)bucket->buffer, STRESS_HASH_N_KEYS);
	/* Make it ASCII range ' '..'_' */
	for (i = 0; i < STRESS_HASH_N_KEYS; i++)
		bucket->buffer[i] = (bucket->buffer[i] & 0x3f) + ' ';

	t1 = stress_time_now();
	for (i = STRESS_HASH_N_KEYS - 1; i; i--) {
		uint32_t hash;

		bucket->buffer[i] = '\0';
		hash = hash_func(bucket->buffer, i);
		i_sum += hash;

		hash %= STRESS_HASH_N_BUCKETS;
		bucket->buckets[hash]++;
		stats->total++;
	}
	t2 = stress_time_now();
	stats->duration += (t2 - t1);

	for (i = 0; i < STRESS_HASH_N_BUCKETS; i++) {
		const double bi = (double)bucket->buckets[i];

		sum += (bi * (bi + 1.0)) / 2.0;
	}
	n = (double)STRESS_HASH_N_KEYS;
	m = (double)STRESS_HASH_N_BUCKETS;
	divisor = (n / (2.0 * m)) * (n + (2.0 * m) - 1.0);

	stats->chi_squared = sum / divisor;

	if (verify && (i_sum != result)) {
		pr_fail("%s: error detected, failed hash checksum %s, "
			"expected %" PRIx32 ", got %" PRIx32 "\n",
			name, hmi->name, result, i_sum);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_hash_method_adler32()
 *	multiple iterations on adler32 hash
 */
static int OPTIMIZE3 stress_hash_method_adler32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_adler32, 0xe0d8c860, 0xe0d8c860);
}

static uint32_t stress_hash_jenkin_wrapper(const char *str, const size_t len)
{
	return (uint32_t)stress_hash_jenkin((const uint8_t *)str, len);
}

/*
 *  stress_hash_method_jenkin()
 *	multiple iterations on jenkin hash
 */
static int OPTIMIZE3 stress_hash_method_jenkin(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_jenkin_wrapper, 0xa6705071, 0xa6705071);
}

static uint32_t stress_hash_murmur3_32_wrapper(const char *str, const size_t len)
{
	const uint32_t seed = 0xf12b35e1; /* arbitrary value */

	return (uint32_t)stress_hash_murmur3_32((const uint8_t *)str, len, seed);
}

/*
 *  stress_hash_method_murmur3_32
 *	 multiple iterations on murmur3_32 hash, based on
 *	 Austin Appleby's Murmur3 hash, code derived from
 *	 https://en.wikipedia.org/wiki/MurmurHash
 */
static int OPTIMIZE3 stress_hash_method_murmur3_32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	/*
	 *  Murmur produces different results depending on the Endianness
	 */
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_murmur3_32_wrapper, 0x54b572fa, 0xc250b788);
}

static uint32_t CONST stress_hash_pjw_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_pjw(str);
}

/*
 *  stress_hash_method_pjw()
 *	stress test hash pjw
 */
static int OPTIMIZE3 stress_hash_method_pjw(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats,bucket, stress_hash_pjw_wrapper, 0xa89a91c0, 0xa89a91c0);
}

static uint32_t CONST stress_hash_djb2a_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_djb2a(str);
}

/*
 *  stress_hash_method_djb2a()
 *	stress test hash djb2a
 */
static int OPTIMIZE3 stress_hash_method_djb2a(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_djb2a_wrapper, 0x6a60cb5a, 0x6a60cb5a);
}

static uint32_t CONST stress_hash_fnv1a_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_fnv1a(str);
}

/*
 *  stress_hash_method_fnv1a()
 *	stress test hash fnv1a
 */
static int OPTIMIZE3 stress_hash_method_fnv1a(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_fnv1a_wrapper, 0x8ef17e80, 0x8ef17e80);
}

static uint32_t CONST stress_hash_sdbm_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_sdbm(str);
}

/*
 *  stress_hash_method_sdbm()
 *	stress test hash sdbm
 */
static int OPTIMIZE3 stress_hash_method_sdbm(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_sdbm_wrapper, 0x46357819, 0x46357819);
}

static uint32_t CONST stress_hash_nhash_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_nhash(str);
}

/*
 *  stress_hash_method_nhash()
 *	stress test hash nhash
 */
static int OPTIMIZE3 stress_hash_method_nhash(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_nhash_wrapper, 0x1cc86e3, 0x1cc86e3);
}

static uint32_t CONST stress_hash_crc32c_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_crc32c(str);
}

/*
 *  stress_hash_method_crc32c()
 *	stress test hash crc32c
 */
static int OPTIMIZE3 stress_hash_method_crc32c(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_crc32c_wrapper, 0x923ab2b3, 0x923ab2b3);
}

static uint32_t CONST OPTIMIZE3 stress_hash_xor(const char *str, const size_t len)
{
	register uint32_t sum = 0;

	(void)len;

	while (*str) {
		register uint32_t top = sum >> 31;

		sum ^= (uint8_t)*str++;
		sum <<= 1;
		sum |= top;
	}
	return sum;
}

/*
 *  stress_hash_method_xor()
 *	simple xor hash
 */
static int OPTIMIZE3 stress_hash_method_xor(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_xor, 0xe6d601eb, 0xe6d601eb);
}

/*
 *  stress_hash_method_muladd32()
 *	simple product hash
 */
static int OPTIMIZE3 stress_hash_method_muladd32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_muladd32, 0x7f0a8d4d, 0x7f0a8d4d);
}

/*
 *  stress_hash_method_muladd64()
 *	simple product hash
 */
static int OPTIMIZE3 stress_hash_method_muladd64(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_muladd64, 0x99109f5c, 0x99109f5c);
}

static uint32_t CONST stress_hash_kandr_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_kandr(str);
}

/*
 *  stress_hash_method_kandr()
 *	stress test hash kandr
 */
static int OPTIMIZE3 stress_hash_method_kandr(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_kandr_wrapper, 0x1e197d9, 0x1e197d9);
}

static uint32_t CONST stress_hash_coffin_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_coffin(str);
}

/*
 *  stress_hash_method_coffin()
 *	stress test hash coffin
 */
static int OPTIMIZE3 stress_hash_method_coffin(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_coffin_wrapper, 0xdc02e07b, 0xdc02e07b);
}

static uint32_t CONST stress_hash_coffin32_wrapper_le(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_coffin32_le(str, len);	/* Little Endian */
}

static uint32_t CONST stress_hash_coffin32_wrapper_be(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_coffin32_be(str, len);	/* Big Endian */
}

/*
 *  stress_hash_method_coffin32()
 *	stress test hash coffin
 */
static int OPTIMIZE3 stress_hash_method_coffin32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	const stress_hash_func wrapper = stress_little_endian() ?
		stress_hash_coffin32_wrapper_le :
		stress_hash_coffin32_wrapper_be;

	return stress_hash_generic(name, hmi, stats, bucket, wrapper, 0xdc02e07b, 0xdc02e07b);
}

static uint32_t CONST stress_hash_x17_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_x17(str);
}

/*
 *  stress_hash_method_x17()
 *	stress test hash x17
 */
static int OPTIMIZE3 stress_hash_method_x17(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_x17_wrapper, 0xd5c97ec8, 0xd5c97ec8);
}

#if defined(HAVE_XXHASH_H) &&	\
    defined(HAVE_LIB_XXHASH)
static uint32_t CONST stress_hash_xxh64_wrapper(const char *str, const size_t len)
{
	return (uint32_t)XXH64(str, len, 0xf261eab7);
}

/*
 *  stress_hash_method_xxh64()
 *	stress test hash xxh64
 */
static int OPTIMIZE3 stress_hash_method_xxh64(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_xxh64_wrapper, 0x5a23bbc6, 0x5a23bbc6);
}
#endif

static uint32_t CONST stress_hash_loselose_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_loselose(str);
}

/*
 *  stress_hash_method_loselose()
 *	stress test hash loselose
 */
static int OPTIMIZE3 stress_hash_method_loselose(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_loselose_wrapper, 0x0007c7e1, 0x0007c7e1);
}

/*
 *  stress_hash_method_knuth()
 *	stress test hash knuth
 */
static int OPTIMIZE3 stress_hash_method_knuth(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_knuth, 0xe944fc94, 0xe944fc94);
}

/*
 *  stress_hash_method_mid5()
 *	stress test hash mid5
 */
static int OPTIMIZE3 stress_hash_method_mid5(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_mid5, 0xe4b74962, 0xe4b74962);
}

/*
 *  stress_hash_method_mulxror32()
 *	stress test hash mulxror32
 */
static int OPTIMIZE3 stress_hash_method_mulxror32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_mulxror32, 0x4d98dd32, 0xf0dce8de);
}

/*
 *  stress_hash_method_mulxror64()
 *	stress test hash mulxror64
 */
static int OPTIMIZE3 stress_hash_method_mulxror64(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_mulxror64, 0x8d38b213, 0x458932cd);
}

/*
 *  stress_hash_method_xorror64()
 *	stress test hash mulxror64
 */
static int OPTIMIZE3 stress_hash_method_xorror64(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_xorror64, 0xe49ed85f, 0x3d414fee);
}

/*
 *  stress_hash_method_xorror32()
 *	stress test hash mulxror32
 */
static int OPTIMIZE3 stress_hash_method_xorror32(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_xorror32, 0x4fddf545, 0x5be5cd40);
}

static uint32_t CONST stress_hash_sedgwick_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_sedgwick(str);
}

static int OPTIMIZE3 stress_hash_method_sedgwick(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_sedgwick_wrapper, 0x266c1ca9, 0x266c1ca9);
}

static uint32_t CONST stress_hash_sobel_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_sobel(str);
}

static int OPTIMIZE3 stress_hash_method_sobel(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	return stress_hash_generic(name, hmi, stats, bucket, stress_hash_sobel_wrapper, 0x2a7cdb61, 0x2a7cdb61);
}

static OPTIMIZE3 int stress_hash_all(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket);

/*
 * Table of has stress methods
 */
static const stress_hash_method_info_t hash_methods[] ALIGN64 = {
	{ "all",		stress_hash_all			},	/* Special "all" test */
	{ "adler32",		stress_hash_method_adler32	},
	{ "coffin",		stress_hash_method_coffin	},
	{ "coffin32",		stress_hash_method_coffin32	},
	{ "crc32c",		stress_hash_method_crc32c	},
	{ "djb2a",		stress_hash_method_djb2a	},
	{ "fnv1a",		stress_hash_method_fnv1a	},
	{ "jenkin",		stress_hash_method_jenkin	},
	{ "kandr",		stress_hash_method_kandr	},
	{ "knuth",		stress_hash_method_knuth	},
	{ "loselose",		stress_hash_method_loselose	},
	{ "mid5",		stress_hash_method_mid5		},
	{ "muladd32",		stress_hash_method_muladd32	},
	{ "muladd64",		stress_hash_method_muladd64	},
	{ "mulxror32",		stress_hash_method_mulxror32	},
	{ "mulxror64",		stress_hash_method_mulxror64	},
	{ "murmur3_32",		stress_hash_method_murmur3_32	},
	{ "nhash",		stress_hash_method_nhash	},
	{ "pjw",		stress_hash_method_pjw		},
	{ "sdbm",		stress_hash_method_sdbm		},
	{ "sedgwick",		stress_hash_method_sedgwick	},
	{ "sobel",		stress_hash_method_sobel	},
	{ "x17",		stress_hash_method_x17		},
	{ "xor",		stress_hash_method_xor		},
	{ "xorror32",		stress_hash_method_xorror32	},
	{ "xorror64",		stress_hash_method_xorror64	},
#if defined(HAVE_XXHASH_H) &&	\
    defined(HAVE_LIB_XXHASH)
	{ "xxh64",		stress_hash_method_xxh64,	},
#endif
};

#define NUM_HASH_METHODS 	(SIZEOF_ARRAY(hash_methods))

static stress_hash_stats_t hash_stats[NUM_HASH_METHODS] ALIGN64;

/*
 *  stress_hash_all()
 *	iterate over all hash stressor methods
 */
static OPTIMIZE3 int stress_hash_all(
	const char *name,
	const stress_hash_method_info_t *hmi,
	stress_hash_stats_t *stats,
	stress_bucket_t *bucket)
{
	static size_t i = 1;	/* Skip over stress_hash_all */
	const stress_hash_method_info_t *hm = &hash_methods[i];
	stress_hash_stats_t *hs = &hash_stats[i];
	int rc;

	(void)hmi;
	(void)stats;

	rc = hm->func(name, hm, hs, bucket);
	i++;
	if (UNLIKELY(i >= NUM_HASH_METHODS))
		i = 1;
	return rc;
}

/*
 *  stress_hash()
 *	stress CPU by doing floating point math ops
 */
static int OPTIMIZE3 stress_hash(stress_args_t *args)
{
	size_t i;
	const stress_hash_method_info_t *hm;
	stress_hash_stats_t *hs;
	size_t hash_method = 0;
	stress_bucket_t bucket;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("hash-method", &hash_method);
	hm = &hash_methods[hash_method];
	hs = &hash_stats[hash_method];

	for (i = 0; i < NUM_HASH_METHODS; i++) {
		hash_stats[i].duration = 0.0;
		hash_stats[i].total = false;
		hash_stats[i].chi_squared = 0.0;
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s'\n", args->name, hm->name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (hm->func(args->name, hm, hs, &bucket) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (stress_instance_zero(args)) {
		pr_block_begin();
		pr_inf("%s: %12.12s %15s %10s\n", args->name, "hash", "hashes/sec", "chi squared");
		for (i = 1; i < NUM_HASH_METHODS; i++) {
			const stress_hash_stats_t *stats = &hash_stats[i];

			if ((stats->duration > 0.0) && (stats->total > 0)) {
				const double rate = (double)((stats->duration > 0.0) ?
					(double)stats->total / stats->duration : (double)0.0);

				pr_inf("%s: %12.12s %15.2f %10.2f\n",
					args->name, hash_methods[i].name, rate, stats->chi_squared);
			}
		}
		pr_block_end();
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const char *stress_hash_method(const size_t i)
{
	return (i < NUM_HASH_METHODS) ? hash_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_hash_method, "hash-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_hash_method },
	END_OPT,
};

const stressor_info_t stress_hash_info = {
	.stressor = stress_hash,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
