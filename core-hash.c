/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR .  See the
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
#include "core-pragma.h"

/*
 *  stress_hash_jenkin()
 *	Jenkin's hash on random data
 *	http://www.burtleburtle.net/bob/hash/doobs.html
 */
uint32_t PURE OPTIMIZE3 stress_hash_jenkin(const uint8_t *data, const size_t len)
{
	register size_t i;
	register uint32_t h = 0;

PRAGMA_UNROLL_N(4)
	for (i = 0; i < len; i++) {
		h += *data++;
		h += h << 10;
		h ^= h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;

	return h;
}

/*
 *  stress_hash_pjw()
 *	Hash a string, from Aho, Sethi, Ullman, Compiling Techniques.
 */
uint32_t PURE OPTIMIZE3 stress_hash_pjw(const char *str)
{
	register uint32_t h = 0;

PRAGMA_UNROLL_N(4)
	while (*str) {
		register uint32_t g;

		h = (h << 4) + (uint32_t)(*str);
		if (0 != (g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
		str++;
	}
	return h;
}

/*
 *  stress_hash_djb2a()
 *	Hash a string, from Dan Bernstein comp.lang.c (xor version)
 */
uint32_t PURE OPTIMIZE3 stress_hash_djb2a(const char *str)
{
	register uint32_t hash = 5381;
	register int c;

PRAGMA_UNROLL_N(4)
	while ((c = *str++)) {
		/* (hash * 33) ^ c */
		hash = ((hash << 5) + hash) ^ (uint32_t)c;
	}
	return hash;
}

/*
 *  stress_hash_fnv1a()
 *	Hash a string, using the improved 32 bit FNV-1a hash
 */
uint32_t PURE OPTIMIZE3 stress_hash_fnv1a(const char *str)
{
	register uint32_t hash = 5381;
	const uint32_t fnv_prime = 16777619; /* 2^24 + 2^9 + 0x93 */
	register int c;

PRAGMA_UNROLL_N(4)
	while ((c = *str++)) {
		hash ^= (uint32_t)c;
		hash *= fnv_prime;
	}
	return hash;
}

/*
 *  stress_hash_sdbm()
 *	Hash a string, using the sdbm data base hash and also
 *	apparently used in GNU awk.
 */
uint32_t PURE OPTIMIZE3 stress_hash_sdbm(const char *str)
{
	register uint32_t hash = 0;
	register int c;

PRAGMA_UNROLL_N(4)
	while ((c = *str++))
		hash = (uint32_t)c + (hash << 6) + (hash << 16) - hash;
	return hash;
}

/*
 *  stress_hash_nhash()
 *	Hash a string using a C implementation of the Exim nhash
 *	algorithm.
 */
uint32_t PURE OPTIMIZE3 stress_hash_nhash(const char *str)
{
	static const uint32_t ALIGN64 primes[] = {
		3, 5, 7, 11, 13, 17, 19, 23, 29, 31,
		37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
		79, 83, 89, 97, 101, 103, 107, 109, 113
	};
	const int n = SIZEOF_ARRAY(primes);
	register int i = 0;
	register uint32_t sum = 0;

PRAGMA_UNROLL_N(4)
	while (*str) {
		i += (n - 1);
		if (i >= n)
			i -= n;

		sum += primes[i] * (uint32_t)*(str++);
	}
	return sum;
}


/*
 *  stress_hash_murmur_32_scramble
 *	helper to scramble bits
 */
static inline uint32_t PURE OPTIMIZE3 stress_hash_murmur_32_scramble(uint32_t k)
{
	k *= 0xcc9e2d51;
	k = (k << 15) | (k >> 17);
	k *= 0x1b873593;
	return k;
}

/*
 *  stress_hash_murmur3_32
 *	32 bit murmur3 hash based on Austin Appleby's
 *	Murmur3 hash, code derived from example code in
 *	https://en.wikipedia.org/wiki/MurmurHash
 */
uint32_t PURE OPTIMIZE3 stress_hash_murmur3_32(
	const uint8_t *key,
	size_t len,
	uint32_t seed)
{
	uint32_t h = seed;
	uint32_t k;
	register size_t i;

	for (i = len >> 2; i; i--) {
		(void)shim_memcpy(&k, key, sizeof(k));
		key += sizeof(k);
		h ^= stress_hash_murmur_32_scramble(k);
		h = (h << 13) | (h >> 19);
		h = h * 5 + 0xe6546b64;
	}

	k = 0;
	for (i = len & 3; i; i--) {
		k <<= 8;
		k |= key[i - 1];
	}
	h ^= stress_hash_murmur_32_scramble(k);
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

/*
 * crc32c table generated using:
 *
 * uint32_t i, crc;
 *
 * for (i = 0; i < 256; i++) {
 *	crc = i;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc = crc & 1 ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
 *	crc32c_table[i] = crc;
 * }
 */
static const uint32_t ALIGN64 crc32c_table[256] = {
	0x00000000, 0xf26b8303, 0xe13b70f7, 0x1350f3f4,
	0xc79a971f, 0x35f1141c, 0x26a1e7e8, 0xd4ca64eb,
	0x8ad958cf, 0x78b2dbcc, 0x6be22838, 0x9989ab3b,
	0x4d43cfd0, 0xbf284cd3, 0xac78bf27, 0x5e133c24,
	0x105ec76f, 0xe235446c, 0xf165b798, 0x030e349b,
	0xd7c45070, 0x25afd373, 0x36ff2087, 0xc494a384,
	0x9a879fa0, 0x68ec1ca3, 0x7bbcef57, 0x89d76c54,
	0x5d1d08bf, 0xaf768bbc, 0xbc267848, 0x4e4dfb4b,
	0x20bd8ede, 0xd2d60ddd, 0xc186fe29, 0x33ed7d2a,
	0xe72719c1, 0x154c9ac2, 0x061c6936, 0xf477ea35,
	0xaa64d611, 0x580f5512, 0x4b5fa6e6, 0xb93425e5,
	0x6dfe410e, 0x9f95c20d, 0x8cc531f9, 0x7eaeb2fa,
	0x30e349b1, 0xc288cab2, 0xd1d83946, 0x23b3ba45,
	0xf779deae, 0x05125dad, 0x1642ae59, 0xe4292d5a,
	0xba3a117e, 0x4851927d, 0x5b016189, 0xa96ae28a,
	0x7da08661, 0x8fcb0562, 0x9c9bf696, 0x6ef07595,
	0x417b1dbc, 0xb3109ebf, 0xa0406d4b, 0x522bee48,
	0x86e18aa3, 0x748a09a0, 0x67dafa54, 0x95b17957,
	0xcba24573, 0x39c9c670, 0x2a993584, 0xd8f2b687,
	0x0c38d26c, 0xfe53516f, 0xed03a29b, 0x1f682198,
	0x5125dad3, 0xa34e59d0, 0xb01eaa24, 0x42752927,
	0x96bf4dcc, 0x64d4cecf, 0x77843d3b, 0x85efbe38,
	0xdbfc821c, 0x2997011f, 0x3ac7f2eb, 0xc8ac71e8,
	0x1c661503, 0xee0d9600, 0xfd5d65f4, 0x0f36e6f7,
	0x61c69362, 0x93ad1061, 0x80fde395, 0x72966096,
	0xa65c047d, 0x5437877e, 0x4767748a, 0xb50cf789,
	0xeb1fcbad, 0x197448ae, 0x0a24bb5a, 0xf84f3859,
	0x2c855cb2, 0xdeeedfb1, 0xcdbe2c45, 0x3fd5af46,
	0x7198540d, 0x83f3d70e, 0x90a324fa, 0x62c8a7f9,
	0xb602c312, 0x44694011, 0x5739b3e5, 0xa55230e6,
	0xfb410cc2, 0x092a8fc1, 0x1a7a7c35, 0xe811ff36,
	0x3cdb9bdd, 0xceb018de, 0xdde0eb2a, 0x2f8b6829,
	0x82f63b78, 0x709db87b, 0x63cd4b8f, 0x91a6c88c,
	0x456cac67, 0xb7072f64, 0xa457dc90, 0x563c5f93,
	0x082f63b7, 0xfa44e0b4, 0xe9141340, 0x1b7f9043,
	0xcfb5f4a8, 0x3dde77ab, 0x2e8e845f, 0xdce5075c,
	0x92a8fc17, 0x60c37f14, 0x73938ce0, 0x81f80fe3,
	0x55326b08, 0xa759e80b, 0xb4091bff, 0x466298fc,
	0x1871a4d8, 0xea1a27db, 0xf94ad42f, 0x0b21572c,
	0xdfeb33c7, 0x2d80b0c4, 0x3ed04330, 0xccbbc033,
	0xa24bb5a6, 0x502036a5, 0x4370c551, 0xb11b4652,
	0x65d122b9, 0x97baa1ba, 0x84ea524e, 0x7681d14d,
	0x2892ed69, 0xdaf96e6a, 0xc9a99d9e, 0x3bc21e9d,
	0xef087a76, 0x1d63f975, 0x0e330a81, 0xfc588982,
	0xb21572c9, 0x407ef1ca, 0x532e023e, 0xa145813d,
	0x758fe5d6, 0x87e466d5, 0x94b49521, 0x66df1622,
	0x38cc2a06, 0xcaa7a905, 0xd9f75af1, 0x2b9cd9f2,
	0xff56bd19, 0x0d3d3e1a, 0x1e6dcdee, 0xec064eed,
	0xc38d26c4, 0x31e6a5c7, 0x22b65633, 0xd0ddd530,
	0x0417b1db, 0xf67c32d8, 0xe52cc12c, 0x1747422f,
	0x49547e0b, 0xbb3ffd08, 0xa86f0efc, 0x5a048dff,
	0x8ecee914, 0x7ca56a17, 0x6ff599e3, 0x9d9e1ae0,
	0xd3d3e1ab, 0x21b862a8, 0x32e8915c, 0xc083125f,
	0x144976b4, 0xe622f5b7, 0xf5720643, 0x07198540,
	0x590ab964, 0xab613a67, 0xb831c993, 0x4a5a4a90,
	0x9e902e7b, 0x6cfbad78, 0x7fab5e8c, 0x8dc0dd8f,
	0xe330a81a, 0x115b2b19, 0x020bd8ed, 0xf0605bee,
	0x24aa3f05, 0xd6c1bc06, 0xc5914ff2, 0x37faccf1,
	0x69e9f0d5, 0x9b8273d6, 0x88d28022, 0x7ab90321,
	0xae7367ca, 0x5c18e4c9, 0x4f48173d, 0xbd23943e,
	0xf36e6f75, 0x0105ec76, 0x12551f82, 0xe03e9c81,
	0x34f4f86a, 0xc69f7b69, 0xd5cf889d, 0x27a40b9e,
	0x79b737ba, 0x8bdcb4b9, 0x988c474d, 0x6ae7c44e,
	0xbe2da0a5, 0x4c4623a6, 0x5f16d052, 0xad7d5351,
};

/*
 *  crc32c the Castagnoli CRC32
 *	lookup table implementation
 */
uint32_t PURE OPTIMIZE3 stress_hash_crc32c(const char *str)
{
	register uint32_t crc = ~0U;
	register uint8_t val;
	register const uint8_t *ptr = (const uint8_t *)str;

PRAGMA_UNROLL_N(4)
	while ((val = *ptr++))
		crc = (crc >> 8) ^ crc32c_table[(crc ^ val) & 0xff];

	return ~crc;
}

/*
 *  stress_hash_adler32()
 *	Mark Adler 32 bit hash
 */
uint32_t PURE OPTIMIZE3 stress_hash_adler32(const char *str, const size_t len)
{
	register const uint32_t mod = 65521;
	register uint32_t a = 1, b = 0;

	(void)len;

PRAGMA_UNROLL_N(4)
	while (*str) {
		a = (a + (uint8_t)*str++) % mod;
		b = (b + a) % mod;
	}
	return (b << 16) | a;
}

/*
 *  stress_hash_muladd32()
 *	simple 32 bit multiply/add hash
 */
uint32_t PURE OPTIMIZE3 stress_hash_muladd32(const char *str, const size_t len)
{
	register uint32_t prod = (uint32_t)len;

PRAGMA_UNROLL_N(4)
	while (*str) {
		register uint32_t top = (prod >> 24);
		prod *= (uint8_t)*str++;
		prod += top;
	}
	return prod;
}

/*
 *  stress_hash_muladd64()
 *	simple 64 bit multiply/add hash
 */
uint32_t PURE OPTIMIZE3 stress_hash_muladd64(const char *str, const size_t len)
{
	register uint64_t prod = len;

PRAGMA_UNROLL_N(4)
	while (*str) {
		register uint64_t top = (prod >> 56);
		prod *= (uint8_t)*str++;
		prod += top;
	}
	return (uint32_t)((prod >> 32) ^ prod);
}

/*
 *  stress_hash_kandr
 *	Kernighan and Ritchie hash, from The C programming Language,
 *	section 6.6, "Hashing" 2nd Edition.
 */
uint32_t PURE OPTIMIZE3 stress_hash_kandr(const char *str)
{
	register uint32_t hash;

PRAGMA_UNROLL_N(4)
	for (hash = 0; *str; str++)
		hash = (uint8_t)*str + 31 * hash;

	return hash;
}

/*
 * stress_hash_coffin()
 *	Coffin hash
 * 	https://stackoverflow.com/a/7666668/5407270
 */
uint32_t PURE OPTIMIZE3 stress_hash_coffin(const char *str)
{
	register uint32_t result = 0x55555555;

	while (*str) {
		result ^= (uint8_t)*str++;
		result = shim_rol32n(result, 5);
	}
	return result;
}

/*
 *  stress_hash_coffin32_le()
 *	Coffin hash, 32 bit optimized fetch, little endian version
 */
uint32_t PURE OPTIMIZE3 stress_hash_coffin32_le(const char *str, const size_t len)
{
	register uint32_t result = 0x55555555;
	register const uint32_t *ptr32 = (const uint32_t *)str;
	register uint32_t val = *ptr32;
	register size_t n = len;

	while (n > 4) {
		register uint32_t tmp;

		tmp = val & 0xff;
		n -= 4;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val >> 8 & 0xff;
		ptr32++;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val >> 16 & 0xff;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val >> 24 & 0xff;
		val = *ptr32;
		result = shim_rol32n(result ^ tmp, 5);
	}

	{
		register const uint8_t *ptr8 = (const uint8_t *)ptr32;

		while (n--) {
			result ^= *ptr8++;
			result = shim_rol32n(result, 5);
		}
	}
	return result;
}

/*
 *  stress_hash_coffin32_be()
 *	Coffin hash, 32 bit optimized fetch, big endian version
 */
uint32_t PURE OPTIMIZE3 stress_hash_coffin32_be(const char *str, const size_t len)
{
	register uint32_t result = 0x55555555;
	register const uint32_t *ptr32 = (const uint32_t *)str;
	register uint32_t val = *ptr32;
	register size_t n = len;

	while (n > 4) {
		register uint32_t tmp;

		tmp = val >> 24 & 0xff;
		n -= 4;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val >> 16 & 0xff;
		ptr32++;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val >> 8 & 0xff;
		result = shim_rol32n(result ^ tmp, 5);
		tmp = val & 0xff;
		val = *ptr32;
		result = shim_rol32n(result ^ tmp, 5);
	}

	{
		register const uint8_t *ptr8 = (const uint8_t *)ptr32;

		while (n--) {
			result ^= *ptr8++;
			result = shim_rol32n(result, 5);
		}
	}
	return result;
}

/*
 * stress_hash_loselose()
 *	Kernighan and Ritchie hash, from The C programming Language,
 *	section 6.6, "Table lookup" 1st Edition.
 */
uint32_t PURE OPTIMIZE3 stress_hash_loselose(const char *str)
{
	register uint32_t hash;

PRAGMA_UNROLL_N(4)
	for (hash = 0; *str; str++) {
		hash += (uint8_t)*str;
	}

	return hash;
}

/*
 * stress_hash_knuth()
 *	Donald E. Knuth in The Art Of Computer Programming Volume 3,
 *	"sorting and search" chapter 6.4.
 */
uint32_t PURE OPTIMIZE3 stress_hash_knuth(const char *str, const size_t len)
{
	register uint32_t hash = (uint32_t)len;

PRAGMA_UNROLL_N(4)
	while (*str) {
		hash = ((hash << 5) ^ (hash >> 27)) ^ ((uint8_t)*str++);
	}

	return hash;
}

/*
 * stress_hash_x17
 *	multiply by 17 hash
 *      https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
 *
 */
uint32_t PURE OPTIMIZE3 stress_hash_x17(const char *str)
{
	register const uint8_t *ptr = (const uint8_t *)str;
	register uint8_t val;
	register uint32_t hash = 0x5179efb3;  /* seed */

PRAGMA_UNROLL_N(4)
	while ((val = *ptr++)) {
		hash = (17 * hash) + (val - ' ');
	}
	return hash ^ (hash >> 16);
}

/*
 * stress_hash_mid5()
 *	middle 5 chars in a string
 */
uint32_t PURE OPTIMIZE3 stress_hash_mid5(const char *str, const size_t len)
{
	const uint8_t *ustr = (const uint8_t *)str;
	switch (len) {
	default:
		ustr += (len - 5) / 2;
		return (uint32_t)len ^ (uint32_t)(ustr[0] ^ (ustr[1] << 6) ^ (ustr[2] << 12) ^ (ustr[3] << 18) ^ (ustr[4] << 24));
	case 4:
		return (uint32_t)len ^ (uint32_t)(ustr[0] ^ (ustr[1] << 6) ^ (ustr[2] << 12) ^ (ustr[3] << 18));
	case 3:
		return (uint32_t)len ^ (uint32_t)(ustr[0] ^ (ustr[1] << 6) ^ (ustr[2] << 12));
	case 2:
		return (uint32_t)len ^ (uint32_t)(ustr[0] ^ (ustr[1] << 6));
	case 1:
		return (uint32_t)len ^ ustr[0];
	}
	return 0;
}

/*
 *  stress_hash_mulxror64()
 *	mangles 64 bits per iteration on fast path, scaling by the 64 bits
 *	from the string and partially rolling right to remix bits back into
 *	the hash. Designed and Implemented Colin Ian King, free to reuse.
 */
uint32_t PURE OPTIMIZE3 stress_hash_mulxror64(const char *str, const size_t len)
{
	register uint64_t hash = len;
	register size_t i;

PRAGMA_UNROLL_N(4)
	for (i = len >> 3; i; i--) {
		uint64_t v;

		/* memcpy optimizes down to a 64 bit load */
		(void)shim_memcpy(&v, str, sizeof(v));
		str += sizeof(v);
		hash *= v;
		hash ^= shim_ror64n(hash, 40);
	}
	for (i = len & 7; i; i--) {
		hash *= (uint8_t)*str++;
		hash ^= shim_ror64n(hash, 5);
	}
	return (uint32_t)((hash >> 32) ^ hash);
}

/*
 *  stress_hash_mulxror32()
 *	mangles 32 bits per iteration on fast path, scaling by the 32 bits
 *	from the string and partially rolling right to remix bits back into
 *	the hash. Designed and Implemented Colin Ian King, free to reuse.
 */
uint32_t PURE OPTIMIZE3 stress_hash_mulxror32(const char *str, const size_t len)
{
	register uint32_t hash = len;
	register size_t i;

PRAGMA_UNROLL_N(4)
	for (i = len >> 2; i; i--) {
		uint32_t v;

		/* memcpy optimizes down to a 32 bit load */
		(void)shim_memcpy(&v, str, sizeof(v));
		str += sizeof(v);
		hash *= v;
		hash ^= shim_ror32n(hash, 20);
	}
	for (i = len & 3; i; i--) {
		hash *= (uint8_t)*str++;
		hash ^= shim_ror32n(hash, 5);
	}
	return hash;
}

/*
 * stress_hash_xorror64()
 *
 */
uint32_t PURE OPTIMIZE3 stress_hash_xorror64(const char *str, const size_t len)
{
	register uint64_t hash = ~(uint64_t)len;
	register size_t i;

PRAGMA_UNROLL_N(4)
	for (i = len >> 3; i; ) {
		uint64_t v64;

		/* memcpy optimizes down to a 64 bit load */
		(void)shim_memcpy(&v64, str, sizeof(v64));
		str += sizeof(v64);
		i--;
		hash = v64 ^ shim_ror64n(hash, 16);
	}
	for (i = len & 7; i;) {
		register uint8_t v8 = *(str++);
		i--;
		hash = v8 ^ shim_ror64n(hash, 2);
	}
	return (uint32_t)((hash >> 32) ^ hash);
}

/*
 * stress_hash_xorror32()
 *
 */
uint32_t PURE OPTIMIZE3 stress_hash_xorror32(const char *str, const size_t len)
{
	register uint32_t hash = ~(uint32_t)len;
	register size_t i;

PRAGMA_UNROLL_N(8)
	for (i = len >> 2; i; ) {
		uint32_t v32;

		/* memcpy optimizes down to a 32 bit load */
		(void)shim_memcpy(&v32, str, sizeof(v32));
		str += sizeof(v32);
		i--;
		hash = v32 ^ shim_ror32n(hash, 4);
	}
	for (i = len & 3; i;) {
		register uint8_t v8 = *(str++);
		i--;
		hash = v8 ^ shim_ror32n(hash, 1);
	}
	return hash;
}


/*
 *  stress_hash_sedgwick()
 *	simple hash from Robert Sedgwicks Algorithms in C book.
 */
uint32_t PURE OPTIMIZE3 stress_hash_sedgwick(const char *str)
{
	const uint32_t b = 378551;
	register uint32_t a = 63689;
	register uint32_t hash = 0;

PRAGMA_UNROLL_N(4)
	while (*str) {
		hash = (hash * a) + (uint8_t)*str++;
		a *= b;
	}
	return hash;
}

/*
 *  stress_hash_sobel()
 *	bitwise hash by Justin Sobel
 */
uint32_t PURE OPTIMIZE3 stress_hash_sobel(const char *str)
{
	register uint32_t hash = 1315423911;

PRAGMA_UNROLL_N(4)
	while (*str) {
		hash ^= ((hash << 5) + (hash >> 2) + (uint8_t)*str++);
	}
	return hash;
}

/*
 *  stress_hash_create()
 *	create a hash table with size of n base hash entries
 */
stress_hash_table_t *stress_hash_create(const size_t n)
{
	stress_hash_table_t *hash_table;

	if (n == 0)
		return NULL;

	hash_table = (stress_hash_table_t *)calloc(1, sizeof(*hash_table));
	if (UNLIKELY(!hash_table))
		return NULL;

	hash_table->table = (stress_hash_t **)calloc(n, sizeof(*(hash_table->table)));
	if (UNLIKELY(!hash_table->table)) {
		free(hash_table);
		return NULL;
	}
	hash_table->n = n;

	return hash_table;
}

#define HASH_STR(hash)	(((char *)hash) + sizeof(*hash))

static inline stress_hash_t *stress_hash_find(stress_hash_t *hash, const char *str)
{
	while (hash) {
		if (!strcmp(str, HASH_STR(hash)))
			return hash;
		hash = hash->next;
	}
	return NULL;
}

/*
 *  stress_hash_get()
 *	get a hash entry based on the given string. returns NULL if it does
 *	not exist
 */
stress_hash_t *stress_hash_get(stress_hash_table_t *hash_table, const char *str)
{
	uint32_t h;

	if (UNLIKELY(!hash_table))
		return NULL;
	if (UNLIKELY(!str))
		return NULL;

	h = stress_hash_sdbm(str) % hash_table->n;
	return stress_hash_find(hash_table->table[h], str);
}

/*
 *  stress_hash_add()
 *	add a hash entry based on the given string. If the string already is
 *	hashed it is not re-added.  Returns null if an error occurs (e.g. out of
 *	memory).
 */
stress_hash_t *stress_hash_add(stress_hash_table_t *hash_table, const char *str)
{
	stress_hash_t *hash;
	uint32_t h;
	size_t len;

	if (UNLIKELY(!hash_table))
		return NULL;
	if (UNLIKELY(!str))
		return NULL;

	h = stress_hash_sdbm(str) % hash_table->n;
	hash = stress_hash_find(hash_table->table[h], str);
	if (hash)
		return hash;

	/* Not found, so add a new hash */
	len = strlen(str) + 1;
	hash = (stress_hash_t *)malloc(sizeof(*hash) + len);
	if (UNLIKELY(!hash))
		return NULL;

	hash->next = hash_table->table[h];
	hash_table->table[h] = hash;
	(void)shim_memcpy(HASH_STR(hash), str, len);

	return hash;
}

/*
 *   stress_hash_delete()
 *	delete a hash table and all entries in the table
 */
void stress_hash_delete(stress_hash_table_t *hash_table)
{
	size_t i;

	if (UNLIKELY(!hash_table))
		return;

	for (i = 0; i < hash_table->n; i++) {
		stress_hash_t *hash = hash_table->table[i];

		while (hash) {
			stress_hash_t *next = hash->next;

			free(hash);
			hash = next;
		}
	}
	free(hash_table->table);
	free(hash_table);
}
