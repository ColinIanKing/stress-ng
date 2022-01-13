/*
 * Copyright (C)      2022 Colin Ian King.
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

typedef struct {
	double_t	duration;
	uint64_t	total;
} stress_hash_stats_t;

struct stress_hash_method_info;

typedef void (*stress_hash_func)(const char *name, const struct stress_hash_method_info *hmi);

typedef struct stress_hash_method_info {
	const char		*name;	/* human readable form of stressor */
	const stress_hash_func	func;	/* the hash method function */
	stress_hash_stats_t	*stats;
} stress_hash_method_info_t;


static const stress_help_t help[] = {
	{ NULL,  "hash N",		"start N workers that exercise various hash functions" },
	{ NULL,  "hash-ops N",		"stop after N hash bogo operations" },
	{ NULL,  "hash-method M",	"specify stress hash method M, default is all" },
	{ NULL,	 NULL,			NULL }
};

static stress_hash_method_info_t hash_methods[];

/*
 *  stress_hash_little_endian()
 *	returns true if CPU is little endian
 */
static inline bool stress_hash_little_endian(void)
{
	const uint32_t x = 0x12345678;
	const uint8_t *y = (const uint8_t *)&x;

	return *y == 0x78;
}

/*
 *  stress_hash_generic()
 *	stress test generic string hash function
 */
static void stress_hash_generic(
	const char *name,
	const stress_hash_method_info_t *hmi,
	uint32_t (*hash_func)(const char *str, const size_t len),
	const uint32_t le_result,
	const uint32_t be_result)
{
	char buffer[128];
	uint32_t i_sum = 0;
	size_t i;
	const uint32_t result = stress_hash_little_endian() ? le_result: be_result;
	stress_hash_stats_t *stats = hmi->stats;
	double t1, t2;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (verify) {
		STRESS_MWC_SEED();
	} else {
		stress_mwc_reseed();
	}

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));
	/* Make it ASCII range ' '..'_' */
	for (i = 0; i < SIZEOF_ARRAY(buffer); i++)
		buffer[i] = (buffer[i] & 0x3f) + ' ';

	t1 = stress_time_now();
	for (i = SIZEOF_ARRAY(buffer) - 1; i; i--) {
		uint32_t hash;
		buffer[i] = '\0';

		hash = hash_func(buffer, i);
		i_sum += hash;
		stats->total++;
	}
	t2 = stress_time_now();
	stats->duration += (t2 - t1);

	if (verify && (i_sum != result))
		pr_fail("%s: error detected, failed hash checksum %s, "
			"expected %" PRIx32 ", got %" PRIx32 "\n",
			name, hmi->name, result, i_sum);
}

uint32_t stress_hash_jenkin_wrapper(const char *str, const size_t len)
{
	return (uint32_t)stress_hash_jenkin((const uint8_t *)str, len);
}

/*
 *  stress_hash_method_jenkin()
 *	multiple iterations on jenkin hash
 */
static void stress_hash_method_jenkin(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_jenkin_wrapper, 0xa6705071, 0xa6705071);
}

uint32_t stress_hash_murmur3_32_wrapper(const char *str, const size_t len)
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
static void stress_hash_method_murmur3_32(const char *name, const struct stress_hash_method_info *hmi)
{
	/*
	 *  Murmur produces different results depending on the Endianness
	 */
	stress_hash_generic(name, hmi, stress_hash_murmur3_32_wrapper, 0x54b572fa, 0xc250b788);
}

uint32_t stress_hash_pjw_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_pjw(str);
}

/*
 *  stress_hash_method_pjw()
 *	stress test hash pjw
 */
static void stress_hash_method_pjw(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_pjw_wrapper, 0xa89a91c0, 0xa89a91c0);
}

uint32_t stress_hash_djb2a_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_djb2a(str);
}

/*
 *  stress_hash_method_djb2a()
 *	stress test hash djb2a
 */
static void stress_hash_method_djb2a(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_djb2a_wrapper, 0x6a60cb5a, 0x6a60cb5a);
}

uint32_t stress_hash_fnv1a_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_fnv1a(str);
}

/*
 *  stress_hash_method_fnv1a()
 *	stress test hash fnv1a
 */
static void HOT stress_hash_method_fnv1a(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_fnv1a_wrapper, 0x8ef17e80, 0x8ef17e80);
}

uint32_t stress_hash_sdbm_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_sdbm(str);
}

/*
 *  stress_hash_method_sdbm()
 *	stress test hash sdbm
 */
static void stress_hash_method_sdbm(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_sdbm_wrapper, 0x46357819, 0x46357819);
}

uint32_t stress_hash_nhash_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_nhash(str);
}

/*
 *  stress_hash_method_nhash()
 *	stress test hash nhash
 */
static void stress_hash_method_nhash(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_nhash_wrapper, 0x1cc86e3, 0x1cc86e3);
}

uint32_t stress_hash_crc32c_wrapper(const char *str, const size_t len)
{
	(void)len;

	return stress_hash_crc32c(str);
}

/*
 *  stress_hash_method_crc32c()
 *	stress test hash crc32c
 */
static void stress_hash_method_crc32c(const char *name, const struct stress_hash_method_info *hmi)
{
	stress_hash_generic(name, hmi, stress_hash_crc32c_wrapper, 0x923ab2b3, 0x923ab2b3);
}

/*
 *  stress_hash_all()
 *	iterate over all hash stressor methods
 */
static HOT OPTIMIZE3 void stress_hash_all(const char *name, const struct stress_hash_method_info *hmi)
{
	static int i = 1;	/* Skip over stress_hash_all */
	const struct stress_hash_method_info *h = &hash_methods[i];

	(void)hmi;

	h->func(name, h);
	i++;
	if (!hash_methods[i].func)
		i = 1;
}

/*
 * Table of has stress methods
 */
static stress_hash_method_info_t hash_methods[] = {
	{ "all",		stress_hash_all,		NULL },	/* Special "all test */
	{ "crc32c",		stress_hash_method_crc32c,	NULL },
	{ "djb2a",		stress_hash_method_djb2a,	NULL },
	{ "fnv1a",		stress_hash_method_fnv1a,	NULL },
	{ "jenkin",		stress_hash_method_jenkin,	NULL },
	{ "murmur3_32",		stress_hash_method_murmur3_32,	NULL },
	{ "nhash",		stress_hash_method_nhash,	NULL },
	{ "pjw",		stress_hash_method_pjw,		NULL },
	{ "sdbm",		stress_hash_method_sdbm,	NULL },
	{ NULL,			NULL,				NULL }
};

stress_hash_stats_t hash_stats[SIZEOF_ARRAY(hash_methods)];

/*
 *  stress_set_hash_method()
 *	set the default hash stress method
 */
static int stress_set_hash_method(const char *name)
{
	size_t i;

	for (i = 0; hash_methods[i].name; i++) {
		if (!strcmp(hash_methods[i].name, name)) {
			stress_set_setting("hash-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "hash-method must be one of:");
	for (i = 0; hash_methods[i].name; i++) {
		(void)fprintf(stderr, " %s", hash_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_hash()
 *	stress CPU by doing floating point math ops
 */
static int HOT OPTIMIZE3 stress_hash(const stress_args_t *args)
{
	size_t i;
	const stress_hash_method_info_t *hm;
	size_t hash_method = 0;
	bool lock = false;

	stress_get_setting("hash-method", &hash_method);
	hm = &hash_methods[hash_method];

	for (i = 0; hash_methods[i].name; i++) {
		hash_stats[i].duration = 0.0;
		hash_methods[i].stats = &hash_stats[i];
	}

	pr_dbg("%s using method '%s'\n", args->name, hm->name);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)hm->func(args->name, hm);
		inc_counter(args);
	} while (keep_stressing(args));

	if (args->instance == 0) {
		pr_lock(&lock);
		pr_inf_lock(&lock, "%s: %12.12s %15s\n",
			args->name, "hash", "hashes/sec");
		for (i = 1; hash_methods[i].name; i++) {
			stress_hash_stats_t *stats = hash_methods[i].stats;
			if (stats->duration > 0.0 && stats->total > 0) {
				pr_inf_lock(&lock, "%s: %12.12s %15.2f\n",
					args->name, hash_methods[i].name,
					stats->duration > 0.0 ? (double)stats->total / stats->duration : 0.0);
			}
		}
		pr_unlock(&lock);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static void stress_hash_set_default(void)
{
	stress_set_hash_method("all");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_hash_method,	stress_set_hash_method },
	{ 0,			NULL },
};

stressor_info_t stress_hash_info = {
	.stressor = stress_hash,
	.set_default = stress_hash_set_default,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
