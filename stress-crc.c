/*
 * Copyright (C) 2026      Colin Ian King
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
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-pragma.h"
#include "core-signal.h"
#include "core-target-clones.h"

#define DATA_ITEMS	(16)
#define DATA_ITEMS8	(DATA_ITEMS << 3)
#define DATA_ITEMS16	(DATA_ITEMS << 2)
#define DATA_ITEMS32	(DATA_ITEMS << 1)
#define DATA_ITEMS64	(DATA_ITEMS << 0)

#define CRC_LOOPS	(64)

#define CRC_64BIT	(ULONG_MAX > 0xffffffffULL)

typedef uint64_t (*stress_crc_func_t)(void);

typedef struct {
	double duration;
	double count;
} crc_metrics_t;

typedef struct {
	stress_crc_func_t	func;
	int			hexwidth;
	uint64_t		result;
	const char 		*name;
} stress_crc_method_t;

static const stress_help_t help[] = {
	{ NULL,	"crc N",	"start N workers performing Cyclic Rundundancy Check ops" },
	{ NULL,	"crc-ops N",	"stop after N Cyclic Rundundancy Check bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_BUILTIN_CRC8_DATA8) ||		\
    defined(HAVE_BUILTIN_CRC16_DATA8) ||	\
    defined(HAVE_BUILTIN_CRC16_DATA16) ||	\
    defined(HAVE_BUILTIN_CRC32_DATA8) ||	\
    defined(HAVE_BUILTIN_CRC32_DATA16) ||	\
    defined(HAVE_BUILTIN_CRC64_DATA8) ||	\
    defined(HAVE_BUILTIN_CRC32_DATA32) ||	\
    defined(HAVE_BUILTIN_CRC64_DATA16) ||	\
    defined(HAVE_BUILTIN_CRC64_DATA32) ||	\
    defined(HAVE_BUILTIN_CRC64_DATA64) ||	\
    defined(HAVE_BUILTIN_REV_CRC8_DATA8) ||	\
    defined(HAVE_BUILTIN_REV_CRC16_DATA8) ||	\
    defined(HAVE_BUILTIN_REV_CRC16_DATA16) ||	\
    defined(HAVE_BUILTIN_REV_CRC32_DATA8) ||	\
    defined(HAVE_BUILTIN_REV_CRC32_DATA16) ||	\
    defined(HAVE_BUILTIN_REV_CRC64_DATA8) ||	\
    defined(HAVE_BUILTIN_REV_CRC32_DATA32) ||	\
    defined(HAVE_BUILTIN_REV_CRC64_DATA16) ||	\
    defined(HAVE_BUILTIN_REV_CRC64_DATA32) ||	\
    defined(HAVE_BUILTIN_REV_CRC64_DATA64)

static const uint64_t crc_data[DATA_ITEMS] ALIGN64 = {
	0x9029e7092a29e4f2ULL, 0x8284f714361ded21ULL,
	0x53f1642c5b7b96e1ULL, 0xf0c5c7d3ecd44d60ULL,
	0x45f3a4e23d5767e0ULL, 0xadbee349c70a1957ULL,
	0xb144f39dda601605ULL, 0x75ed8279ec8fe1dcULL,
	0x716ee09523c02a98ULL, 0x15aa1d2f901b0463ULL,
	0xc84278f4f2ba8574ULL, 0xa3428d78ba9d2a27ULL,
	0x0d90fd0bb99f2103ULL, 0x1ae768700ef64c11ULL,
	0x6e1287ff21e34e94ULL, 0xec142b9a0484d1b5ULL,
};

#if defined(HAVE_BUILTIN_CRC8_DATA8)
/*
 *  stress_crc_crc8_data8()
 *  	8 bit crc with 8 bit data, Hamming distace 6
 */
uint64_t OPTIMIZE3 stress_crc_crc8_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint8_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_crc8_data8(crc, data[i], 0x9bU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC16_DATA8)
/*
 *  stress_crc_crc16_data8()
 *  	16 bit crc with 8 bit data, Hamming distace 10
 */
uint64_t OPTIMIZE3 stress_crc_crc16_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint16_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_crc16_data8(crc, data[i], 0xed2fU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC16_DATA16)
/*
 *  stress_crc_crc16_data16()
 *  	16 bit crc with 16 bit data, Hamming distace 10
 */
uint64_t OPTIMIZE3 stress_crc_crc16_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint16_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_crc16_data16(crc, data[i], 0xed2fU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC32_DATA8)
/*
 *  stress_crc_crc32_data8()
 *  	32 bit crc with 8 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_crc32_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_crc32_data8(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC32_DATA16)
/*
 *  stress_crc_crc32_data16()
 *  	32 bit crc with 16 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_crc32_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_crc32_data16(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC32_DATA32)
/*
 *  stress_crc_crc32_data32()
 *  	32 bit crc with 32 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_crc32_data32(void)
{
	register int i;
	uint32_t *data = (uint32_t *)crc_data;
	uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS32; i++)
		crc = __builtin_crc32_data32(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if CRC_64BIT
#if defined(HAVE_BUILTIN_CRC64_DATA8)
/*
 *  stress_crc_crc64_data8()
 *  	64 bit crc with 8 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_crc64_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_crc64_data8(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC64_DATA16)
/*
 *  stress_crc_crc64_data16()
 *  	64 bit crc with 16 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_crc64_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_crc64_data16(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC64_DATA32)
/*
 *  stress_crc_crc64_data32()
 *  	64 bit crc with 32 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_crc64_data32(void)
{
	register int i;
	register uint32_t *data = (uint32_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS32; i++)
		crc = __builtin_crc64_data32(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_CRC64_DATA64)
/*
 *  stress_crc_crc64_data64()
 *  	64 bit crc with 64 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_crc64_data64(void)
{
	register int i;
	register uint64_t *data = (uint64_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS64; i++)
		crc = __builtin_crc64_data64(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif
#endif

#if defined(HAVE_BUILTIN_REV_CRC8_DATA8)
/*
 *  stress_crc_rev_crc8_data8()
 *  	reverse 8 bit crc with 8 bit data, Hamming distace 6
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc8_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint8_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_rev_crc8_data8(crc, data[i], 0x9bU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC16_DATA8)
/*
 *  stress_crc_rev_crc16_data8()
 *  	reverse 16 bit crc with 8 bit data, Hamming distace 10
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc16_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint16_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_rev_crc16_data8(crc, data[i], 0xed2fU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC16_DATA16)
/*
 *  stress_crc_rev_crc16_data16()
 *  	reverse 16 bit crc with 16 bit data, Hamming distace 10
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc16_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint16_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_rev_crc16_data16(crc, data[i], 0xed2fU);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC32_DATA8)
/*
 *  stress_crc_rev_crc32_data8()
 *  	revsere 32 bit crc with 8 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc32_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_rev_crc32_data8(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC32_DATA16)
/*
 *  stress_crc_rev_crc32_data16()
 *  	revsere 32 bit crc with 16 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc32_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_rev_crc32_data16(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC32_DATA32)
/*
 *  stress_crc_rev_crc32_data32()
 *  	revsere 32 bit crc with 32 bit data, Hamming distace 18
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc32_data32(void)
{
	register int i;
	register uint32_t *data = (uint32_t *)crc_data;
	register uint32_t crc = 0;

	for (i = 0; i < DATA_ITEMS32; i++)
		crc = __builtin_rev_crc32_data32(crc, data[i], 0x973afb51UL);
	return (uint64_t)crc;
}
#endif

#if CRC_64BIT
#if defined(HAVE_BUILTIN_REV_CRC64_DATA8)
/*
 *  stress_crc_rev_crc64_data8()
 *  	revserse 64 bit crc with 8 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc64_data8(void)
{
	register int i;
	register uint8_t *data = (uint8_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS8; i++)
		crc = __builtin_rev_crc64_data8(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC64_DATA16)
/*
 *  stress_crc_rev_crc64_data16()
 *  	revserse 64 bit crc with 16 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc64_data16(void)
{
	register int i;
	register uint16_t *data = (uint16_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS16; i++)
		crc = __builtin_rev_crc64_data16(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC64_DATA32)
/*
 *  stress_crc_rev_crc64_data32()
 *  	revserse 64 bit crc with 32 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc64_data32(void)
{
	register int i;
	register uint32_t *data = (uint32_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS32; i++)
		crc = __builtin_rev_crc64_data32(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif

#if defined(HAVE_BUILTIN_REV_CRC64_DATA64)
/*
 *  stress_crc_rev_crc64_data64()
 *  	revserse 64 bit crc with 64 bit data, Hamming distace 97(?)
 */
uint64_t OPTIMIZE3 stress_crc_rev_crc64_data64(void)
{
	register int i;
	register uint64_t *data = (uint64_t *)crc_data;
	register uint64_t crc = 0;

	for (i = 0; i < DATA_ITEMS64; i++)
		crc = __builtin_rev_crc64_data64(crc, data[i], 0xa17870f5d4f51b49ULL);
	return crc;
}
#endif
#endif

#if defined(STRESS_ARCH_LE)
static const stress_crc_method_t stress_crc_methods[] = {
#if defined(HAVE_BUILTIN_CRC8_DATA8)
	{ stress_crc_crc8_data8,	2, 0x00000000000000b6ULL, "crc8_data8" },
#endif
#if defined(HAVE_BUILTIN_CRC16_DATA8)
	{ stress_crc_crc16_data8,	4, 0x0000000000009877ULL, "crc16_data8" },
#endif
#if defined(HAVE_BUILTIN_CRC16_DATA16)
	{ stress_crc_crc16_data16,	4, 0x0000000000008081ULL, "crc16_data16" },
#endif
#if defined(HAVE_BUILTIN_CRC32_DATA8)
	{ stress_crc_crc32_data8,	8, 0x00000000682f174fULL, "crc32_data8" },
#endif
#if defined(HAVE_BUILTIN_CRC32_DATA16)
	{ stress_crc_crc32_data16,	8, 0x00000000c16d11efULL, "crc32_data16" },
#endif
#if defined(HAVE_BUILTIN_CRC32_DATA32)
	{ stress_crc_crc32_data32,	8, 0x000000005eb90bdeULL, "crc32_data32" },
#endif
#if CRC_64BIT
#if defined(HAVE_BUILTIN_CRC64_DATA8)
	{ stress_crc_crc64_data8,	16, 0xb983aacb1ba2159fULL, "crc64_data8" },
#endif
#if defined(HAVE_BUILTIN_CRC64_DATA16)
	{ stress_crc_crc64_data16,	16, 0xd38077a8e61f358dULL, "crc64_data16" },
#endif
#if defined(HAVE_BUILTIN_CRC64_DATA32)
	{ stress_crc_crc64_data32,	16, 0x7552ec85bdde72e5ULL, "crc64_data32" },
#endif
#if defined(HAVE_BUILTIN_CRC64_DATA64)
	{ stress_crc_crc64_data64,	16, 0xa0f7e0ff180fe1e5ULL, "crc64_data64" },
#endif
#endif
#if defined(HAVE_BUILTIN_REV_CRC8_DATA8)
	{ stress_crc_rev_crc8_data8,	2, 0x0000000000000086ULL, "rev_crc8_data8" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC16_DATA8)
	{ stress_crc_rev_crc16_data8,	4, 0x00000000000097aaULL, "rev_crc16_data8" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC16_DATA16)
	{ stress_crc_rev_crc16_data16,	4, 0x00000000000097aaULL, "rev_crc16_data16" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC32_DATA8)
	{ stress_crc_rev_crc32_data8,	8, 0x000000001d8e0895ULL, "rev_crc32_data8" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC32_DATA16)
	{ stress_crc_rev_crc32_data16,	8, 0x000000001d8e0895ULL, "rev_crc32_data16" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC32_DATA32)
	{ stress_crc_rev_crc32_data32,	8, 0x000000001d8e0895ULL, "rev_crc32_data32" },
#endif
#if CRC_64BIT
#if defined(HAVE_BUILTIN_REV_CRC64_DATA8)
	{ stress_crc_rev_crc64_data8,	16, 0xb5fab4d316a5537bULL, "rev_crc64_data8" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC64_DATA16)
	{ stress_crc_rev_crc64_data16,	16, 0xb5fab4d316a5537bULL, "rev_crc64_data16" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC64_DATA32)
	{ stress_crc_rev_crc64_data32,	16, 0xb5fab4d316a5537bULL, "rev_crc64_data32" },
#endif
#if defined(HAVE_BUILTIN_REV_CRC64_DATA64)
	{ stress_crc_rev_crc64_data64,	16, 0xb5fab4d316a5537bULL, "rev_crc64_data64" },
#endif
#endif
};
#else
static const stress_crc_method_t stress_crc_methods[] = {
	{ stress_crc_crc8_data8,	2, 0x0000000000000025ULL, "crc8_data8" },
	{ stress_crc_crc16_data8,	4, 0x000000000000a567ULL, "crc16_data8" },
	{ stress_crc_crc16_data16,	4, 0x000000000000a567ULL, "crc16_data16" },
	{ stress_crc_crc32_data8,	8, 0x00000000db982926ULL, "crc32_data8" },
	{ stress_crc_crc32_data16,	8, 0x00000000db982926ULL, "crc32_data16" },
	{ stress_crc_crc32_data32,	8, 0x00000000db982926ULL, "crc32_data32" },
#if CRC_64BIT
	{ stress_crc_crc64_data8,	16, 0xa0f7e0ff180fe1e5ULL, "crc64_data8" },
	{ stress_crc_crc64_data16,	16, 0xa0f7e0ff180fe1e5ULL, "crc64_data16" },
	{ stress_crc_crc64_data32,	16, 0xa0f7e0ff180fe1e5ULL, "crc64_data32" },
	{ stress_crc_crc64_data64,	16, 0xa0f7e0ff180fe1e5ULL, "crc64_data64" },
#endif
	{ stress_crc_rev_crc8_data8,	2, 0x0000000000000073ULL, "rev_crc8_data8" },
	{ stress_crc_rev_crc16_data8,	4, 0x0000000000006293ULL, "rev_crc16_data8" },
	{ stress_crc_rev_crc16_data16,	4, 0x000000000000d9faULL, "rev_crc16_data16" },
	{ stress_crc_rev_crc32_data8,	8, 0x00000000affbe53eULL, "rev_crc32_data8" },
	{ stress_crc_rev_crc32_data16,	8, 0x00000000021e163eULL, "rev_crc32_data16" },
	{ stress_crc_rev_crc32_data32,	8, 0x00000000bcc82743ULL, "rev_crc32_data32" },
#if CRC_64BIT
	{ stress_crc_rev_crc64_data8,	16, 0x3e7fe6b87a88aae9ULL, "rev_crc64_data8" },
	{ stress_crc_rev_crc64_data16,	16, 0x5dab8969d3ae5996ULL, "rev_crc64_data16" },
	{ stress_crc_rev_crc64_data32,	16, 0x625ff866b3c6ad1cULL, "rev_crc64_data32" },
	{ stress_crc_rev_crc64_data64,	16, 0xb5fab4d316a5537bULL, "rev_crc64_data64" },
#endif
};
#endif

#define N_CRC_METHODS	(SIZEOF_ARRAY(stress_crc_methods))

static int stress_crc(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	size_t i, j;
	bool failed = false;

	crc_metrics_t metrics[N_CRC_METHODS] ALIGN64;

	for (i = 0; i < N_CRC_METHODS; i++) {
		metrics[i].count = 0.0;
		metrics[i].duration = 0.0;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; (i < N_CRC_METHODS) && stress_continue(args); i++) {
			register uint64_t result;
			register int loop;
			const double t = stress_time_now();

			for (loop = 0; (loop < CRC_LOOPS); loop++) {
				result = stress_crc_methods[i].func();
				if (UNLIKELY(result != stress_crc_methods[i].result)) {
					const int hexwidth = stress_crc_methods[i].hexwidth;

					pr_inf("%s: '%s' failed, got 0x%*.*" PRIx64 ", expecting 0x%*.*" PRIx64 "\n",
						args->name, stress_crc_methods[i].name,
						hexwidth, hexwidth, result,
						hexwidth, hexwidth, stress_crc_methods[i].result);
					failed = true;
					break;
				}
			}
			metrics[i].duration += stress_time_now() - t;
			metrics[i].count += (double)CRC_LOOPS;

			stress_bogo_inc(args);
		}
	} while (!failed && stress_continue(args));

	for (i = 0, j = 0; i < N_CRC_METHODS; i++) {
		if (metrics[i].duration > 0.0) {
			char buf[64];
			const double rate = metrics[i].count / metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops/sec", stress_crc_methods[i].name);
			stress_metrics_set(args, j++, buf, rate, STRESS_METRIC_GEOMETRIC_MEAN);
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_crc_info = {
	.stressor = stress_crc,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

const stressor_info_t stress_crc_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without __builtin_crc functionality"
};

#endif
