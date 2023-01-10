/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-cpu.h"
#include "core-target-clones.h"

static const stress_help_t help[] = {
	{ NULL,	"zlib N",		"start N workers compressing data with zlib" },
	{ NULL,	"zlib-level L",		"specify zlib compression level 0=fast, 9=best" },
	{ NULL,	"zlib-mem-level L",	"specify zlib compression state memory usage 1=minimum, 9=maximum" },
	{ NULL,	"zlib-method M",	"specify zlib random data generation method M" },
	{ NULL,	"zlib-ops N",		"stop after N zlib bogo compression operations" },
	{ NULL,	"zlib-strategy S",	"specify zlib strategy 0=default, 1=filtered, 2=huffman only, 3=rle, 4=fixed" },
	{ NULL,	"zlib-stream-bytes S",	"specify the number of bytes to deflate until the current stream will be closed" },
	{ NULL,	"zlib-window-bits W",	"specify zlib window bits -8-(-15) | 8-15 | 24-31 | 40-47" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_Z)

#include "zlib.h"

#define DATA_SIZE_64K 	(KB * 64)	/* Must be a multiple of 64 bytes */
#define DATA_SIZE DATA_SIZE_64K

typedef void (*stress_zlib_rand_data_func)(const stress_args_t *args,
	uint64_t *RESTRICT data, uint64_t *RESTRICT data_end);

typedef struct {
	const char *name;			/* human readable form of random data generation selection */
	const stress_zlib_rand_data_func func;	/* the random data generation function */
} stress_zlib_rand_data_info_t;

typedef struct {
	uint64_t	zlib_checksum;
	uint64_t	xchars;
	bool		error;
	bool		pipe_broken;
	bool		interrupted;
} stress_zlib_checksum_t;

typedef struct {
	stress_zlib_rand_data_info_t	*info;	/* data generator info */
	int32_t		window_bits;	/* zlib window bits */
	uint32_t	level;		/* zlib compression level */
	uint32_t	mem_level;	/* zlib memory usage */
	uint32_t	strategy;	/* zlib strategy */
	uint64_t	stream_bytes;	/* size of generated data until deflate should generate Z_STREAM_END */
} stress_zlib_args_t;

typedef struct morse {
	char ch;
	char *str;
} morse_t;

static const morse_t ALIGN64 morse[] = {
	{ 'a', ".-" },
	{ 'b', "-.." },
	{ 'c', "-.-." },
	{ 'd', "-.." },
	{ 'e', "." },
	{ 'f', "..-." },
	{ 'g', "--." },
	{ 'h', "...." },
	{ 'i', ".." },
	{ 'j', ".---" },
	{ 'k', "-.-" },
	{ 'l', ".-.." },
	{ 'm', "--" },
	{ 'n', "-." },
	{ 'o', "---" },
	{ 'p', ".--." },
	{ 'q', "--.-" },
	{ 'r', ".-." },
	{ 's', "..." },
	{ 't', "-" },
	{ 'u', "..-" },
	{ 'v', "...-" },
	{ 'w', ".--" },
	{ 'x', "-..-" },
	{ 'y', "-.--" },
	{ 'z', "--.." },
	{ '0', "-----" },
	{ '1', ".----" },
	{ '2', "..---" },
	{ '3', "...--" },
	{ '4', "....-" },
	{ '5', "....." },
	{ '6', "-...." },
	{ '7', "--..." },
	{ '8', "---.." },
	{ '9', "----." },
	{ ' ', " " }
};

static const stress_zlib_rand_data_info_t zlib_rand_data_methods[];
static volatile bool pipe_broken = false;
static sigjmp_buf jmpbuf;

static const char *const lorem_ipsum[] = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
	"Nullam imperdiet quam at ultricies finibus. ",
	"Nunc venenatis euismod velit sit amet ornare.",
	"Quisque et orci eu eros convallis luctus at facilisis ex. ",
	"Quisque fringilla nulla felis, sed mollis est feugiat nec. ",
	"Vivamus at urna sit amet velit suscipit iaculis. ",
	"Curabitur mauris ipsum, gravida in laoreet ac, dignissim id lacus. ",
	"Proin dignissim, erat nec interdum commodo, nulla mi tempor dui, quis scelerisque odio nisi in tortor. ",
	"Mauris dignissim ex auctor nulla lobortis semper. ",
	"Mauris sit amet tempus risus, ac tincidunt lectus. ",
	"Maecenas sollicitudin porttitor nisi ac faucibus. ",
	"Cras eu sollicitudin arcu. ",
	"In sed fringilla eros, vitae fringilla tortor. ",
	"Phasellus mollis feugiat tortor, a ornare nunc auctor porttitor. ",
	"Fusce malesuada ut felis vitae vestibulum. ",
	"Donec sit amet hendrerit massa, vitae ultrices augue. ",
	"Proin volutpat velit ipsum, id imperdiet risus placerat ut. ",
	"Cras vitae risus ipsum.  ",
	"Sed lobortis quam in dictum pulvinar. ",
	"In non accumsan justo. ",
	"Ut pretium pulvinar gravida. ",
	"Proin ultricies nisi libero, a convallis dui vestibulum eu. ",
	"Aliquam in molestie magna, et ullamcorper turpis. ",
	"Donec id pharetra sem.  ",
	"Duis dui massa, fringilla id mattis nec, consequat eget felis. ",
	"Integer a lobortis ipsum, quis ornare felis. ",
	"Class aptent taciti sociosqu ad litora torquent per conubia nostra, per inceptos himenaeos. ",
	"Nulla sed cursus nibh. ",
	"Quisque at ex dolor. ",
	"Mauris viverra risus pellentesque nisl dictum rutrum. ",
	"Aliquam non est quis enim dictum tristique. ",
	"Fusce feugiat hendrerit hendrerit. ",
	"Ut egestas sed erat et egestas. ",
	"Pellentesque convallis erat sed sapien pellentesque vulputate. ",
	"Praesent non sapien aliquet risus varius suscipit. ",
	"Curabitur eu felis dignissim, hendrerit magna vitae, condimentum nunc. ",
	"Donec ut tincidunt sem. ",
	"Sed in leo et metus ultricies semper quis quis ex. ",
	"Sed fringilla porta mi vitae condimentum. ",
	"In vitae metus libero."
};

/*
 *  stress_sigpipe_handler()
 *      SIGFPE handler
 */
static void MLOCKED_TEXT stress_sigpipe_handler(int signum)
{
	(void)signum;

	pipe_broken = true;
}

static void NORETURN MLOCKED_TEXT stress_bad_read_handler(int signum)
{
	(void)signum;

	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_rand_data_bcd()
 *	fill buffer with random binary coded decimal digits
 */
static void stress_rand_data_bcd(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register uint32_t r = stress_mwc32();
		register uint32_t t = (r * 0x290) >> 16;
		/* v = r % 100 using multiplication rather than division */
		register uint32_t v = r - (t * 100);
		register uint32_t d1 = (v * 0x199a) >> 16;
		/* d1 = v / 10 using multiplication rather than division */
		register uint8_t  d0 = (uint8_t)(v - (d1 * 10));

		/* d0 = v % 10 using multiplication rather than division */
		*ptr++ = (uint8_t)(d1 << 4 | d0);
	}
}

/*
 *  stress_rand_data_utf8()
 *	fill buffer with random bytes converted into utf8
 */
static void TARGET_CLONES stress_rand_data_utf8(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register const uint8_t ch = stress_mwc8();

		if (ch <= 0x7f)
			*ptr++ = ch;
		else {
			if (UNLIKELY(ptr < end - 1)) {
				*ptr++ = ((ch >> 6) & 0x1f) | 0xc0;
				*ptr++ = (ch & 0x3f) | 0x80;
			} else {
				/* no more space, zero pad */
				*ptr = 0;
			}
		}
	}
}

/*
 *  stress_rand_data_binary()
 *	fill buffer with random binary data
 */
static void stress_rand_data_binary(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	(void)args;

	while (ptr < end) {
		*(ptr++) = stress_mwc32();
		*(ptr++) = stress_mwc32();
		*(ptr++) = stress_mwc32();
		*(ptr++) = stress_mwc32();
	}
}

/*
 *  stress_rand_data_text()
 *	fill buffer with random ASCII text
 */
static void stress_rand_data_text(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	const size_t size = (size_t)((uintptr_t)data_end - (uintptr_t)data);

	(void)args;
	stress_rndstr((char *)data, size);
}

/*
 *  stress_rand_data_01()
 *	fill buffer with random ASCII 0 or 1
 */
static void TARGET_CLONES stress_rand_data_01(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register uint8_t v = stress_mwc8();

		*(ptr + 0) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 1) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 2) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 3) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 4) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 5) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 6) = '0' + (v & 1);
		v >>= 1;
		*(ptr + 7) = '0' + (v & 1);
		ptr += 8;
	}
}

/*
 *  stress_rand_data_digits()
 *	fill buffer with random ASCII '0' .. '9'
 */
static void stress_rand_data_digits(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register uint32_t v = stress_mwc32();

		*(ptr++) = '0' + ((v & 0xff) % 10);
		v >>= 8;
		*(ptr++) = '0' + ((v & 0xff) % 10);
		v >>= 8;
		*(ptr++) = '0' + ((v & 0xff) % 10);
		v >>= 8;
		*(ptr++) = '0' + ((v & 0xff) % 10);
	}
}

/*
 *  stress_rand_data_00_ff()
 *	fill buffer with random 0x00 or 0xff
 */
static void TARGET_CLONES stress_rand_data_00_ff(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	(void)args;

	while (ptr < end) {
		register const uint8_t v = stress_mwc8();

		*(ptr + 0) = (v & 1) ? 0x00 : 0xff;
		*(ptr + 1) = (v & 2) ? 0x00 : 0xff;
		*(ptr + 2) = (v & 4) ? 0x00 : 0xff;
		*(ptr + 3) = (v & 8) ? 0x00 : 0xff;
		*(ptr + 4) = (v & 16) ? 0x00 : 0xff;
		*(ptr + 5) = (v & 32) ? 0x00 : 0xff;
		*(ptr + 6) = (v & 64) ? 0x00 : 0xff;
		*(ptr + 7) = (v & 128) ? 0x00 : 0xff;

		ptr += 8;
	}
}

/*
 *  stress_rand_data_nybble()
 *	fill buffer with 0x00..0x0f
 */
static void TARGET_CLONES stress_rand_data_nybble(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register uint32_t v = stress_mwc32();

		*(ptr + 0) = v & 0xf;
		v >>= 4;
		*(ptr + 1) = v & 0xf;
		v >>= 4;
		*(ptr + 2) = v & 0xf;
		v >>= 4;
		*(ptr + 3) = v & 0xf;
		v >>= 4;
		*(ptr + 4) = v & 0xf;
		v >>= 4;
		*(ptr + 5) = v & 0xf;
		v >>= 4;
		*(ptr + 6) = v & 0xf;
		v >>= 4;
		*(ptr + 7) = v & 0xf;

		ptr += 8;
	}
}

/*
 *  stress_rand_data_rarely_1()
 *	fill buffer with data that is 1 in every 32 bits 1
 */
static void TARGET_CLONES stress_rand_data_rarely_1(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	(void)args;

	while (ptr < end) {
		*(ptr++) = 1 << (stress_mwc32() & 0x1f);
		*(ptr++) = 1 << (stress_mwc32() & 0x1f);
		*(ptr++) = 1 << (stress_mwc32() & 0x1f);
		*(ptr++) = 1 << (stress_mwc32() & 0x1f);
	}
}

/*
 *  stress_rand_data_rarely_0()
 *	fill buffer with data that is 1 in every 32 bits 0
 */
static void TARGET_CLONES stress_rand_data_rarely_0(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	(void)args;

	while (ptr < end) {
		*(ptr++) = ~(1 << (stress_mwc32() & 0x1f));
		*(ptr++) = ~(1 << (stress_mwc32() & 0x1f));
		*(ptr++) = ~(1 << (stress_mwc32() & 0x1f));
		*(ptr++) = ~(1 << (stress_mwc32() & 0x1f));
	}
}

/*
 *  stress_rand_data_fixed()
 *	fill buffer with data that is 0x04030201
 */
static void TARGET_CLONES stress_rand_data_fixed(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	(void)args;

	while (ptr < end) {
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
		*(ptr++) = 0x04030201;
	}
}

#if defined(HAVE_ASM_X86_RDRAND) &&		\
    defined(STRESS_ARCH_X86_64)
/*
 *  rdrand64()
 *	read 64 bit random value
 */
static inline uint64_t rand64(void)
{
	uint64_t        ret;

	__asm__ __volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret;
}

/*
 *  stress_rand_data_rdrand()
 *	fill buffer with data from x86 rdrand instruction
 */
static void stress_rand_data_rdrand(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint64_t *ptr = data;
	register const uint64_t *end = data_end;

	(void)args;

	if (stress_cpu_x86_has_rdrand()) {
		while (ptr < end) {
			*(ptr++) = rand64();
			*(ptr++) = rand64();
			*(ptr++) = rand64();
			*(ptr++) = rand64();
		}
	} else {
		while (ptr < end) {
			*(ptr++) = stress_mwc64();
			*(ptr++) = stress_mwc64();
			*(ptr++) = stress_mwc64();
			*(ptr++) = stress_mwc64();
		}
	}
}
#endif

#define ROR32(x, n) x = (((x) >> n) | ((x) << (32 - n)))

static void TARGET_CLONES stress_rand_data_ror32(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint16_t *ptr = (uint16_t *)data;
	register const uint16_t *end = (uint16_t *)data_end;

	(void)args;

	while (ptr < end) {
		register uint32_t val = stress_mwc32();

		ptr[0x00] = (uint16_t)val;
		ROR32(val, 1);
		ptr[0x01] = (uint16_t)val;
		ROR32(val, 2);
		ptr[0x02] = (uint16_t)val;
		ROR32(val, 3);
		ptr[0x03] = (uint16_t)val;
		ROR32(val, 4);
		ptr[0x04] = (uint16_t)val;
		ROR32(val, 5);
		ptr[0x05] = (uint16_t)val;
		ROR32(val, 6);
		ptr[0x06] = (uint16_t)val;
		ROR32(val, 7);
		ptr[0x07] = (uint16_t)val;
		ptr += 8;
	}
}

/*
 *  stress_rand_data_double()
 *	fill buffer with double precision floating point binary data
 */
static void TARGET_CLONES stress_rand_data_double(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	static double theta = 0.0;
	double dtheta = M_PI / 180.0;

	register double *ptr = (double *)data;
	register const double *end = (double *)data_end;

	(void)args;

	while (ptr < end) {
		const double s = sin(theta);

		(void)memcpy((void *)ptr, &s, sizeof(*ptr));

		theta += dtheta;
		dtheta += 0.001;
		ptr++;
	}
}

/*
 *  stress_rand_data_gray()
 *	fill buffer with gray code of incrementing 16 bit values
 *
 */
static void TARGET_CLONES stress_rand_data_gray(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	static uint16_t val = 0;
	register uint16_t *ptr = (uint16_t *)data;
	register const uint16_t *end = (uint16_t *)data_end;
	register const uint16_t v = val;
	register uint32_t i;

	(void)args;

	for (i = 0; ptr < end; ) {
		*(ptr++) = (uint16_t)((v >> 1) ^ i);
		i++;
		*(ptr++) = (uint16_t)((v >> 1) ^ i);
		i++;
		*(ptr++) = (uint16_t)((v >> 1) ^ i);
		i++;
		*(ptr++) = (uint16_t)((v >> 1) ^ i);
		i++;
	}

	val = v;
}


/*
 *  stress_rand_data_parity()
 *	fill buffer with 7 bit data + 1 parity bit
 */
static void TARGET_CLONES stress_rand_data_parity(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		register const uint8_t v = stress_mwc8();
		register uint8_t p = v & 0xfe;

		p ^= p >> 4;
		p &= 0xf;
		p = (0x6996 >> p) & 1;

		*(ptr++) = v | p;
	}
}


#define PINK_MAX_ROWS   (12)
#define PINK_BITS       (16)
#define PINK_SHIFT      ((sizeof(uint64_t) * 8) - PINK_BITS)

#if !defined(HAVE_BUILTIN_CTZ)
/*
 *  stress_builtin_ctz()
 *	implementation of count trailing zeros ctz, this will
 *	be optimized down to 1 instruction on x86 targets
 */
static inline uint32_t TARGET_CLONES stress_builtin_ctz(register uint32_t x)
{
	register unsigned int n;
	if (!x)
		return 32;

	n = 0;
	if ((x & 0x0000ffff) == 0) {
		n += 16;
		x >>= 16;
	}
	if ((x & 0x000000ff) == 0) {
		n += 8;
		x >>= 8;
	}
	if ((x & 0x0000000f) == 0) {
		n += 4;
		x >>= 4;
	}
	if ((x & 0x00000003) == 0) {
		n += 2;
		x >>= 2;
	}
	if ((x & 0x00000001) == 0) {
		n += 1;
	}
	return n;
}
#endif

/*
 *  stress_rand_data_pink()
 *	fill buffer with pink noise 0..255 using an
 *	the Gardner method with the McCartney
 *	selection tree optimization
 */
static void TARGET_CLONES stress_rand_data_pink(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;
	size_t idx = 0;
	const size_t mask = (1 << PINK_MAX_ROWS) - 1;
	uint64_t sum = 0;
	const uint64_t max = (PINK_MAX_ROWS + 1) * (1 << (PINK_BITS - 1));
	uint64_t rows[PINK_MAX_ROWS];
	const float scalar = 256.0F / (float)max;

	(void)args;
	(void)memset(rows, 0, sizeof(rows));

	while (ptr < end) {
		int64_t rnd;

		idx = (idx + 1) & mask;
		if (idx) { /* cppcheck-suppress knownConditionTrueFalse */
#if defined(HAVE_BUILTIN_CTZ)
			const size_t j = (size_t)__builtin_ctz(idx);
#else
			const size_t j = (size_t)stress_builtin_ctz(idx);
#endif

			sum -= rows[j];
			rnd = (int64_t)stress_mwc64() >> PINK_SHIFT;
			sum += rnd;
			rows[j] = (uint64_t)rnd;
		}
		rnd = (int64_t)stress_mwc64() >> PINK_SHIFT;
		*(ptr++) = (uint8_t)(int)((scalar * (float)((int64_t)sum + rnd)) + 128.0F);
	}
}

/*
 *  stress_rand_data_brown()
 *	fills buffer with brown noise.
 */
static void TARGET_CLONES stress_rand_data_brown(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	static uint8_t val = 127;
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;
	register uint8_t v = val;

	(void)args;

	while (ptr < end) {
		register uint32_t rnd = stress_mwc32();

		v += (((rnd & 0xff) % 31) - 15);
		val = v;
		*ptr++ = v;
		rnd >>= 8;

		v += (((rnd & 0xff) % 31) - 15);
		val = v;
		*ptr++ = v;
		rnd >>= 8;

		v += (((rnd & 0xff) % 31) - 15);
		val = v;
		*ptr++ = v;
		rnd >>= 8;

		v += (((rnd & 0xff) % 31) - 15);
		val = v;
		*ptr++ = v;
	}
}

/*
 *  stress_rand_data_logmap()
 *	fills buffer with output from a logistic map of
 *	x = r * x * (x - 1.0) where r is the accumulation point
 *	based on A098587. Data is scaled in the range 0..255
 */
static void TARGET_CLONES stress_rand_data_logmap(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	double x = 0.4;
	/*
	 * Use an accumulation point that is slightly larger
	 * than the point where chaotic behaviour starts
	 */
	const double r = 3.569945671870944901842 * 1.0999999;
	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	while (ptr < end) {
		/*
		 *  Scale up a fractional part of x
		 *  by an arbitrary value and take
		 *  the bottom 8 bits of the result
		 *  to make a quasi-chaotic random-ish
		 *  value
		 */
		double v = x - (int)x;
		v *= 1278178.381817673;

		*(ptr++) = (uint8_t)v;
		x = x * r * (1.0 - x);
	}
}

/*
 *  stress_rand_data_lfsr32()
 *	fills buffer with 2^32-1 values (all 2^32 except for zero)
 *	using the Galois polynomial: x^32 + x^31 + x^29 + x + 1
 */
static void TARGET_CLONES stress_rand_data_lfsr32(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;
	static uint32_t s_lfsr = 0xf63acb01;
	register uint32_t lfsr = s_lfsr;

	(void)args;

	while (ptr < end) {
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr++) = lfsr;
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr++) = lfsr;
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr++) = lfsr;
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr++) = lfsr;
	}
	s_lfsr = lfsr;
}

/*
 *  stress_rand_data_gcr()
 *	fills buffer with random data expanded from 4 to 5 bits
 *	using Group coded recording.
 */
static void TARGET_CLONES stress_rand_data_gcr(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	/* CBM 2040 GCR */
	static const uint8_t ALIGN64 gcr45[] = {
		0x0a, 0x0b, 0x12, 0x13,
		0x0e, 0x0f, 0x16, 0x17,
		0x09, 0x19, 0x1a, 0x1b,
		0x0d, 0x1d, 0x1e, 0x15
	};

	register uint8_t *ptr = (uint8_t *)data;
	register const uint8_t *end = (uint8_t *)data_end;

	(void)args;

	for (;;) {
		register uint32_t rnd = stress_mwc32();
		register uint64_t gcr;

		/* 32 bits to 40 bits */
		gcr = gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];
		gcr <<= 5;
		rnd >>= 4;
		gcr |= gcr45[rnd & 0xf];

		*ptr++ = (uint8_t)(gcr >> 32);
		if (ptr >= end)
			break;
		*ptr++ = (uint8_t)(gcr >> 24);
		if (ptr >= end)
			break;
		*ptr++ = (uint8_t)(gcr >> 16);
		if (ptr >= end)
			break;
		*ptr++ = (uint8_t)(gcr >> 8);
		if (ptr >= end)
			break;
		*ptr++ = (uint8_t)gcr >> 0;
		if (ptr >= end)
			break;
	}
}

#if defined(HAVE_INT128_T)
/*
 *  stress_rand_data_lehmer()
 *	fills buffer with random data from lehmer
 *	random number generator, see:
 *
 *      D. H. Lehmer; Mathematical methods in large-scale computing units.
 *      Proceedings of a Second Symposium on Large Scale Digital
 *	Calculating Machinery. Annals of the Computation Laboratory,
 *	Harvard Univ. 26 (1951), pp. 141-146.
 *
 * 	P L'Ecuyer; Tables of linear congruential generators of different
 *	sizes and good lattice structure. Mathematics of Computation of
 *	the American Mathematical Society 68.225 (1999): 249-260.
 */
static void TARGET_CLONES stress_rand_data_lehmer(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	static __uint128_t state;
	static bool seeded = false;
	register uint64_t *ptr, *end;

	(void)args;

	if (UNLIKELY(!seeded)) {
		register uint32_t *ptr32 = (uint32_t *)&state;

		*ptr32++ = stress_mwc32();
		*ptr32++ = stress_mwc32();
		*ptr32++ = stress_mwc32();
		*ptr32++ = stress_mwc32();
		seeded = true;
	}

	ptr = (uint64_t *)data;
	end = (uint64_t *)data_end;

	while (ptr < end) {
		state *= 0xda942042e4dd58b5ULL;
		*(ptr++) = (state >> 64);
		state *= 0xda942042e4dd58b5ULL;
		*(ptr++) = (state >> 64);
		state *= 0xda942042e4dd58b5ULL;
		*(ptr++) = (state >> 64);
		state *= 0xda942042e4dd58b5ULL;
		*(ptr++) = (state >> 64);
	}
}
#endif

#if defined(HAVE_SRAND48) &&	\
    defined(HAVE_LRAND48)
/*
 *  stress_rand_data_lrand48()
 *	fills buffer with random data from lrand48
 */
static void stress_rand_data_lrand48(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	static bool seeded = false;
	register uint32_t *ptr = (uint32_t *)data;
	register const uint32_t *end = (uint32_t *)data_end;

	if (UNLIKELY(!seeded)) {
		srand48(stress_mwc32());
		seeded = true;
	}

	(void)args;

	while (ptr < end) {
		*(ptr++) = (uint32_t)lrand48();
		*(ptr++) = (uint32_t)lrand48();
		*(ptr++) = (uint32_t)lrand48();
		*(ptr++) = (uint32_t)lrand48();
	}
}
#endif

/*
 *  stress_rand_data_latin()
 *	fill buffer with random latin Lorum Ipsum text.
 */
static void stress_rand_data_latin(
	const stress_args_t *args,
	uint64_t *data,
	uint64_t *data_end)
{
	static const char *ptr = NULL;
	char *dataptr = (char *)data;
	char const *end = (char *)data_end;

	(void)args;

	if (!ptr)
		ptr = lorem_ipsum[stress_mwc32modn(SIZEOF_ARRAY(lorem_ipsum))];

	while (dataptr < end) {
		if (!*ptr)
			ptr = lorem_ipsum[stress_mwc32modn(SIZEOF_ARRAY(lorem_ipsum))];

		*dataptr++ = *ptr++;
	}
}

/*
 *  stress_rand_data_morse()
 *	fill buffer with morse encoded random latin Lorum Ipsum text.
 */
static void stress_rand_data_morse(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	register size_t i;
	static const char *ptr = NULL;
	char *dataptr = (char *)data;
	static char ALIGN64 *morse_table[256];
	static bool morse_table_init = false;
	const size_t size = (size_t)((uintptr_t)data_end - (uintptr_t)data);

	(void)args;

	if (!morse_table_init) {
		(void)memset(morse_table, 0, sizeof(morse_table));
		for (i = 0; i < (int)SIZEOF_ARRAY(morse); i++)
			morse_table[tolower((int)morse[i].ch)] = morse[i].str;
		morse_table_init = true;
	}

	ptr = "";
	for (i = 0; i < size; ) {
		register int ch;
		register char *mptr;

		if (!*ptr)
			ptr = lorem_ipsum[stress_mwc32modn(SIZEOF_ARRAY(lorem_ipsum))];

		ch = tolower((int)*ptr);
		mptr = morse_table[ch];
		if (mptr) {
			for (; *mptr && (i < size); mptr++) {
				*dataptr++ = *mptr;
				i++;
			}
			for (mptr = " "; *mptr && (i < size); mptr++) {
				*dataptr++ = *mptr;
				i++;
			}
		}
		ptr++;
	}
}


/*
 *  stress_rand_data_objcode()
 *	fill buffer with object code data from stress-ng
 */
static void stress_rand_data_objcode(
	const stress_args_t *args,
	uint64_t *RESTRICT const data,
	uint64_t *RESTRICT data_end)
{
	static bool use_rand_data = false;
	struct sigaction sigsegv_orig, sigbus_orig;
	char *text, *dataptr;
	char *text_start, *text_end;

	if (use_rand_data) {
		stress_rand_data_binary(args, data, data_end);
		return;
	}

	/* Try and install sighandlers */
	if (stress_sighandler(args->name, SIGSEGV, stress_bad_read_handler, &sigsegv_orig) < 0) {
		use_rand_data = true;
		stress_rand_data_binary(args, data, data_end);
		return;
	}
	if (stress_sighandler(args->name, SIGBUS, stress_bad_read_handler, &sigbus_orig) < 0) {
		use_rand_data = true;
		(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
		stress_rand_data_binary(args, data, data_end);
		return;
	}

	/*
	 *  Some architectures may generate faults on reads
	 *  from specific text pages, so trap these and
	 *  fall back to stress_rand_data_binary.
	 */
	if (sigsetjmp(jmpbuf, 1) != 0) {
		(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
		(void)stress_sigrestore(args->name, SIGBUS, &sigbus_orig);
		stress_rand_data_binary(args, data, data_end);
		return;
	}

	if ((char *)stress_rand_data_bcd < (char *)stress_rand_data_objcode) {
		text_start = (char *)stress_rand_data_bcd;
		text_end = (char *)stress_rand_data_objcode;
	} else {
		text_start = (char *)stress_rand_data_objcode;
		text_end = (char *)stress_rand_data_bcd;
	}
	text = text_start + (stress_mwc64modn((uint64_t)(text_end - text_start)));

	for (dataptr = (char *)data; dataptr < (char *)data_end; dataptr++) {
		*dataptr = *text;
		if (text++ >= text_end)
			text = text_start;
	}
	(void)stress_sigrestore(args->name, SIGSEGV, &sigsegv_orig);
	(void)stress_sigrestore(args->name, SIGBUS, &sigbus_orig);
}

/*
 *  stress_rand_data_zero()
 *	fill buffer with zeros
 */
static void stress_rand_data_zero(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	(void)args;

	while (data < data_end) {
		*data++ = 0;
		*data++ = 0;
		*data++ = 0;
		*data++ = 0;
	}
}

static void stress_zlib_random_test(const stress_args_t *args, uint64_t *RESTRICT data, uint64_t *RESTRICT data_end);

/*
 * Table of zlib data methods
 */
static const stress_zlib_rand_data_info_t zlib_rand_data_methods[] = {
	{ "random",	stress_zlib_random_test }, /* Special "random" test */
	{ "00ff",	stress_rand_data_00_ff },
	{ "ascii01",	stress_rand_data_01 },
	{ "asciidigits",stress_rand_data_digits },
	{ "bcd",	stress_rand_data_bcd },
	{ "binary",	stress_rand_data_binary },
	{ "brown",	stress_rand_data_brown },
	{ "double",	stress_rand_data_double },
	{ "gcr",	stress_rand_data_gcr },
	{ "gray",	stress_rand_data_gray },
	{ "fixed",	stress_rand_data_fixed },
	{ "latin",	stress_rand_data_latin },
#if defined(HAVE_INT128_T)
	{ "lehmer",	stress_rand_data_lehmer },
#endif
	{ "logmap",	stress_rand_data_logmap },
	{ "lfsr32",	stress_rand_data_lfsr32 },
#if defined(HAVE_SRAND48) &&	\
    defined(HAVE_LRAND48)
	{ "lrand48",	stress_rand_data_lrand48 },
#endif
	{ "morse",	stress_rand_data_morse },
	{ "nybble",	stress_rand_data_nybble },
	{ "objcode",	stress_rand_data_objcode },
	{ "parity",	stress_rand_data_parity },
	{ "pink",	stress_rand_data_pink },
	{ "rarely1",	stress_rand_data_rarely_1 },
	{ "rarely0",	stress_rand_data_rarely_0 },
#if defined(HAVE_ASM_X86_RDRAND) &&		\
    defined(STRESS_ARCH_X86_64)
	{ "rdrand",	stress_rand_data_rdrand },
#endif
	{ "ror32",	stress_rand_data_ror32 },
	{ "text",	stress_rand_data_text },
	{ "utf8",	stress_rand_data_utf8 },
	{ "zero",	stress_rand_data_zero },
	{ NULL,		NULL }
};

/*
 *  stress_zlib_random_test()
 *	randomly select data generation function
 */
static void stress_zlib_random_test(
	const stress_args_t *args,
	uint64_t *RESTRICT data,
	uint64_t *RESTRICT data_end)
{
	/* We ignore 1st method (random) and last (NULL) entry */
	const int max = SIZEOF_ARRAY(zlib_rand_data_methods) - 2;
	const int idx = (stress_mwc32modn(max)) + 1;

	zlib_rand_data_methods[idx].func(args, data, data_end);
}

/*
 *  stress_set_zlib_level
 *	set zlib compression level, 0..9,
 *	0 = no compression, 1 = fastest, 9 = best compression
 */
static int stress_set_zlib_level(const char *opt)
{
	uint32_t zlib_level;

	zlib_level = stress_get_uint32(opt);
	stress_check_range("zlib-level", (uint64_t)zlib_level, 0, Z_BEST_COMPRESSION);
	return stress_set_setting("zlib-level", TYPE_ID_UINT32, &zlib_level);
}

/*
 *  stress_set_zlib_mem_level
 *	set the amount of reserved memory for the compression state, 1..9,
 *	1 = minimum, 9 = maximum
 */
static int stress_set_zlib_mem_level(const char *opt)
{
	uint32_t zlib_mem_level;

	zlib_mem_level = stress_get_uint32(opt);
	stress_check_range("zlib-mem-level", (uint64_t)zlib_mem_level, 1, 9);
	return stress_set_setting("zlib-mem-level", TYPE_ID_UINT32, &zlib_mem_level);
}

/*
 *  stress_set_zlib_method()
 *	set the default zlib random data method
 */
static int stress_set_zlib_method(const char *opt)
{
	const stress_zlib_rand_data_info_t *info;

	for (info = zlib_rand_data_methods; info->func; info++) {
		if (!strcmp(info->name, opt)) {
			stress_set_setting("zlib-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "zlib-method must be one of:");
	for (info = zlib_rand_data_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_set_zlib_window_bits
 *	specify the window bits used to allocate the history buffer size. The value is
 * 	specified as the base two logarithm of the buffer size (e.g. value 9 is 2^9 =
 * 	512 bytes).
 * 	8-(-15): raw format
 * 	   8-15: zlib format
 * 	  24-31: gzip format (zlib format +16)
 * 	  40-47: autodetect format when using inflate (zlib format +32)
 *	         hint: stress-ng uses zlib format as default for deflate
 */
static int stress_set_zlib_window_bits(const char *opt)
{
	int32_t zlib_window_bits;

	zlib_window_bits = stress_get_int32(opt);
	if (zlib_window_bits > 31) {
		/* auto detect inflate format */
		stress_check_range("zlib-window-bits", (uint64_t)zlib_window_bits, 40, 47);
	} else if (zlib_window_bits > 15) {
		/* gzip format */
		stress_check_range("zlib-window-bits", (uint64_t)zlib_window_bits, 24, 31);
	} else if (zlib_window_bits > 0) {
		/* zlib format */
		stress_check_range("zlib-window-bits", (uint64_t)zlib_window_bits, 8, 15);
	} else {
		stress_check_range("zlib-window-bits", (uint64_t)zlib_window_bits, -15, -8);
	}
	return stress_set_setting("zlib-window-bits", TYPE_ID_INT32, &zlib_window_bits);
}

/*
 *  stress_set_zlib_stream_bytes
 *	create chunks instead of an endless deflate stream
 */
static int stress_set_zlib_stream_bytes(const char *opt)
{
	uint64_t zlib_stream_bytes;

	zlib_stream_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("zlib-stream-bytes", zlib_stream_bytes, 0, MAX_MEM_LIMIT);
	return stress_set_setting("zlib-stream-bytes", TYPE_ID_UINT64, &zlib_stream_bytes);
}

/*
 *  stress_set_zlib_strategy
 *	set the zlib compression strategy to be used for compression
 */
static int stress_set_zlib_strategy(const char *opt)
{
	uint32_t zlib_strategy;

	zlib_strategy = stress_get_uint32(opt);
	stress_check_range("zlib-strategy", (uint64_t)zlib_strategy, Z_DEFAULT_STRATEGY, Z_FIXED);
	return stress_set_setting("zlib-strategy", TYPE_ID_UINT32, &zlib_strategy);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_zlib_level,		stress_set_zlib_level },
	{ OPT_zlib_mem_level,		stress_set_zlib_mem_level },
	{ OPT_zlib_method,		stress_set_zlib_method },
	{ OPT_zlib_window_bits,		stress_set_zlib_window_bits },
	{ OPT_zlib_stream_bytes,	stress_set_zlib_stream_bytes },
	{ OPT_zlib_strategy,		stress_set_zlib_strategy },
	{ 0,				NULL }
};

/*
 *  stress_zlib_err()
 *	turn a zlib error to something human readable
 */
static const char *stress_zlib_err(const int zlib_err)
{
	static char buf[1024];

	switch (zlib_err) {
	case Z_OK:
		return "no error";
	case Z_ERRNO:
		(void)snprintf(buf, sizeof(buf), "system error, errno=%d (%s)\n",
			errno, strerror(errno));
		return buf;
	case Z_STREAM_ERROR:
		return "inconsistent stream or invalid parameter level/memory/strategy/window-bits (Z_STREAM_ERROR)";
	case Z_DATA_ERROR:
		return "invalid or incomplete deflate data or stream was freed prematurely (Z_DATA_ERROR)";
	case Z_MEM_ERROR:
		return "out of memory (Z_MEM_ERROR)";
	case Z_VERSION_ERROR:
		return "zlib version mismatch (Z_VERSION_ERROR)";
	default:
		(void)snprintf(buf, sizeof(buf), "unknown zlib error %d\n", zlib_err);
		return buf;
	}
}

/*
 *  stress_zlib_get_args()
 *	get all zlib arguments at once
 */
static void stress_zlib_get_args(stress_zlib_args_t *params) {
	(void)stress_get_setting("zlib-level", &params->level);
	(void)stress_get_setting("zlib-mem-level", &params->mem_level);
	(void)stress_get_setting("zlib-method", &params->info);
	(void)stress_get_setting("zlib-window-bits", &params->window_bits);
	(void)stress_get_setting("zlib-stream-bytes", &params->stream_bytes);
	(void)stress_get_setting("zlib-strategy", &params->strategy);
}

/*
 *  stress_zlib_inflate()
 *	inflate compressed data out of the read
 *	end of a pipe fd
 */
static int stress_zlib_inflate(
	const stress_args_t *args,
	const int fd,
	const int zlib_checksum_fd)
{
	ssize_t sz;
	int ret, err = 0;
	z_stream stream_inf;
	static unsigned char ALIGN64 in[DATA_SIZE];
	static unsigned char ALIGN64 out[DATA_SIZE];
	stress_zlib_checksum_t zlib_checksum;
	stress_zlib_args_t zlib_args;

	(void)stress_zlib_get_args(&zlib_args);

	(void)memset(&zlib_checksum, 0, sizeof(zlib_checksum));
	zlib_checksum.zlib_checksum = 0;
	zlib_checksum.xchars = 0;
	zlib_checksum.error = false;
	zlib_checksum.pipe_broken = false;
	zlib_checksum.interrupted = false;

	(void)memset(&stream_inf, 0, sizeof(stream_inf));
	stream_inf.zalloc = Z_NULL;
	stream_inf.zfree = Z_NULL;
	stream_inf.opaque = Z_NULL;
	stream_inf.avail_in = 0;
	stream_inf.next_in = Z_NULL;

	pr_dbg("INF: lvl=%d mem-lvl=%d wbits=%d strategy=%d\n",
		zlib_args.level, zlib_args.mem_level, zlib_args.window_bits,
		zlib_args.strategy);
	do {
		ret = inflateInit2(&stream_inf, zlib_args.window_bits);
		if (ret != Z_OK) {
			pr_fail("%s: zlib inflateInit error: %s\n",
				args->name, stress_zlib_err(ret));
			zlib_checksum.error = true;
			goto zlib_checksum_error;
		}

		do {
			int def_size;

			/* read buffer size first */
			sz = stress_read_buffer(fd, &def_size, sizeof(def_size), false);
			if (sz == 0) {
				break;
			} else if (sz != sizeof(def_size)) {
				(void)inflateEnd(&stream_inf);
				if ((errno != EINTR) && (errno != EPIPE)) {
					pr_fail("%s: zlib pipe read size error: %s (ret=%zd errno=%d)\n",
						args->name, strerror(errno), sz, errno);
					zlib_checksum.error = true;
					goto zlib_checksum_error;
				} else {
					if (errno == EINTR)
						zlib_checksum.interrupted = true;
					if (errno == EPIPE)
						zlib_checksum.pipe_broken = true;
					goto finish;
				}
			}
			/* read deflated buffer */
			sz = stress_read_buffer(fd, in, def_size, false);
			if (sz == 0) {
				break;
			} else if (sz != def_size) {
				(void)inflateEnd(&stream_inf);
				if ((errno != EINTR) && (errno != EPIPE)) {
					pr_fail("%s: zlib pipe read buffer error: %s (ret=%zd errno=%d)\n",
						args->name, strerror(errno), sz, errno);
					zlib_checksum.error = true;
					goto zlib_checksum_error;
				} else {
					if (errno == EINTR)
						zlib_checksum.interrupted = true;
					if (errno == EPIPE)
						zlib_checksum.pipe_broken = true;
					goto finish;
				}
			}

			stream_inf.avail_in = (unsigned int)sz;
			stream_inf.next_in = in;
			do {
				stream_inf.avail_out = DATA_SIZE;
				stream_inf.next_out = out;

				ret = inflate(&stream_inf, Z_NO_FLUSH);
				switch (ret) {
				case Z_BUF_ERROR:
					break;
				case Z_NEED_DICT:
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					pr_fail("%s: zlib inflate error: %s\n",
						args->name, stress_zlib_err(ret));
					(void)inflateEnd(&stream_inf);
					goto zlib_checksum_error;
				}

				if (g_opt_flags & OPT_FLAGS_VERIFY) {
					size_t i;

					for (i = 0; i < DATA_SIZE - stream_inf.avail_out; i++) {
						zlib_checksum.zlib_checksum += (uint64_t)out[i];
						zlib_checksum.xchars++;
					}
				}
			} while (stream_inf.avail_out == 0);
		} while (ret != Z_STREAM_END);

		(void)inflateEnd(&stream_inf);
		stream_inf.zalloc = Z_NULL;
		stream_inf.zfree = Z_NULL;
		stream_inf.opaque = Z_NULL;
	} while (sz > 0);

finish:
	if (write(zlib_checksum_fd, &zlib_checksum, sizeof(zlib_checksum)) < 0) {
		pr_fail("%s: zlib inflate pipe write failed for zlib_checksum, errno=%d (%s)\n",
			args->name, err, strerror(err));
	}
	return ((ret == Z_OK) || (ret == Z_STREAM_END)) ?
		EXIT_SUCCESS : EXIT_FAILURE;

zlib_checksum_error:
	zlib_checksum.error = true;
	if (write(zlib_checksum_fd, &zlib_checksum, sizeof(zlib_checksum)) < 0) {
		pr_fail("%s: zlib inflate pipe write failed, errno=%d (%s)\n",
			args->name, err, strerror(err));
	}
	return EXIT_FAILURE;
}

/*
 *  stress_zlib_deflate()
 *	compress random data and write it down the
 *	write end of a pipe fd
 */
static int stress_zlib_deflate(
	const stress_args_t *args,
	const int fd,
	const int zlib_checksum_fd)
{
	uint64_t stream_bytes_out = 0;
	int ret, err = 0;
	z_stream stream_def;
	uint64_t bytes_in = 0, bytes_out = 0;
	int flush;
	stress_zlib_args_t zlib_args;
	double t1, duration, rate, ratio;
	stress_zlib_checksum_t zlib_checksum;
	stress_zlib_rand_data_info_t *info;

	(void)stress_zlib_get_args(&zlib_args);
	info = (stress_zlib_rand_data_info_t *)zlib_args.info;

	(void)memset(&zlib_checksum, 0, sizeof(zlib_checksum));
	zlib_checksum.zlib_checksum = 0;
	zlib_checksum.xchars = 0;
	zlib_checksum.error = false;
	zlib_checksum.pipe_broken = false;
	zlib_checksum.interrupted = false;

	(void)memset(&stream_def, 0, sizeof(stream_def));
	stream_def.zalloc = Z_NULL;
	stream_def.zfree = Z_NULL;
	stream_def.opaque = Z_NULL;

	t1 = stress_time_now();

	/* default to zlib format if inflate auto detect has been used */
	if (zlib_args.window_bits > 31)
		zlib_args.window_bits = zlib_args.window_bits - 32;

	pr_dbg("DEF: lvl=%d mem-lvl=%d wbits=%d strategy=%d stream-bytes=%llu\n",
		zlib_args.level, zlib_args.mem_level, zlib_args.window_bits,
		zlib_args.strategy, (unsigned long long)zlib_args.stream_bytes);
	do {
		ret = deflateInit2(&stream_def, zlib_args.level,
				Z_DEFLATED, zlib_args.window_bits,
				zlib_args.mem_level, zlib_args.strategy);
		if (ret != Z_OK) {
			pr_fail("%s: zlib deflateInit error: %s\n",
				args->name, stress_zlib_err(ret));
			zlib_checksum.error = true;
			ret = EXIT_FAILURE;
			goto zlib_checksum_error;
		}

		stream_bytes_out = 0;
		do {
			static uint64_t ALIGN64 in[DATA_SIZE / sizeof(uint64_t)];
			uint64_t *in_end = (uint64_t *)((uintptr_t)&in + sizeof(in));
			unsigned char *zlib_checksum_in = (unsigned char *)in;
			const uint64_t diff = zlib_args.stream_bytes - stream_bytes_out;

			int gen_sz = (int)((diff >= DATA_SIZE)
					|| (diff == 0) /* cppcheck-suppress knownConditionTrueFalse */
					|| (zlib_args.stream_bytes == 0)) ? (int)DATA_SIZE : (int)diff;

			if (zlib_args.stream_bytes > 0) {
				flush = (stream_bytes_out + (uint64_t)gen_sz < zlib_args.stream_bytes)
					&& keep_stressing(args)
					? Z_NO_FLUSH : Z_FINISH;
			} else {
				flush = keep_stressing(args) ? Z_NO_FLUSH : Z_FINISH;
			}

			info->func(args, in, in_end);

			stream_def.avail_in = (unsigned int)gen_sz;
			stream_def.next_in = (unsigned char *)in;

			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				int i;

				for (i = 0; i < gen_sz; i++) {
					zlib_checksum.zlib_checksum += zlib_checksum_in[i];
					zlib_checksum.xchars++;
				}
			}

			bytes_in += DATA_SIZE;
			do {
				static unsigned char ALIGN64 out[DATA_SIZE];
				ssize_t sz;
				int def_size;

				stream_def.avail_out = DATA_SIZE;
				stream_def.next_out = out;

				ret = deflate(&stream_def, flush);
				if (ret == Z_STREAM_ERROR) {
					pr_fail("%s: zlib deflate error: %s\n",
						args->name, stress_zlib_err(ret));
					(void)deflateEnd(&stream_def);
					ret = EXIT_FAILURE;
					goto zlib_checksum_error;
				}
				def_size = (int)(DATA_SIZE - stream_def.avail_out);
				bytes_out += (uint64_t)def_size;
				stream_bytes_out += (uint64_t)((def_size) ? gen_sz : 0);

				/* continue if nothing has been deflated */
				if (def_size == 0)
					continue;

				/* write buffer length value */
				sz = stress_write_buffer(fd, &def_size, sizeof(def_size), true);
				if (sz == 0) {
					break;
				} else if (sz != sizeof(def_size)) {
					(void)deflateEnd(&stream_def);
					if ((errno != EINTR) && (errno != EPIPE) && (errno != 0)) {
						pr_fail("%s: zlib pipe write size error: %s (ret=%zd errno=%d)\n",
							args->name, strerror(errno), sz, errno);
						ret = EXIT_FAILURE;
						goto zlib_checksum_error;
					} else {
						if (errno == EINTR)
							zlib_checksum.interrupted = true;
						if (errno == EPIPE)
							zlib_checksum.pipe_broken = true;
						goto finish;
					}
				}
				/* write deflate buffer */
				sz = stress_write_buffer(fd, out, def_size, true);
				if (sz == 0) {
					break;
				} else if (sz != def_size) {
					(void)deflateEnd(&stream_def);
					if ((errno != EINTR) && (errno != EPIPE) && (errno != 0)) {
						pr_fail("%s: zlib pipe write buffer error: %s (ret=%zd errno=%d)\n",
							args->name, strerror(errno), sz, errno);
						ret = EXIT_FAILURE;
						goto zlib_checksum_error;
					} else {
						if (errno == EINTR)
							zlib_checksum.interrupted = true;
						if (errno == EPIPE)
							zlib_checksum.pipe_broken = true;
						goto finish;
					}
				}
				inc_counter(args);
			} while (stream_def.avail_out == 0);
		} while (ret != Z_STREAM_END);

		(void)deflateEnd(&stream_def);
		stream_def.zalloc = Z_NULL;
		stream_def.zfree = Z_NULL;
		stream_def.opaque = Z_NULL;
	} while (keep_stressing(args));

finish:
	duration = stress_time_now() - t1;

	ratio = (bytes_in > 0) ? 100.0 * (double)bytes_out / (double)bytes_in : 0.0;
	stress_metrics_set(args, 0, "% compression ratio", ratio);
	rate = (duration > 0.0) ? ((double)bytes_in / duration) / MB : 0.0;
	stress_metrics_set(args, 1, "MB/sec compression rate", rate);

	ret = EXIT_SUCCESS;
zlib_checksum_error:
	if (write(zlib_checksum_fd, &zlib_checksum, sizeof(zlib_checksum)) < 0 ) {
		pr_fail("%s: zlib deflate pipe write error for zlib_checksum: errno=%d (%s)\n",
			args->name, err, strerror(err));
	}

	return ret;
}

/*
 *  stress_zlib()
 *	stress cpu with compression and decompression
 */
static int stress_zlib(const stress_args_t *args)
{
	int ret = EXIT_SUCCESS, fds[2];
	int deflate_zlib_checksum_fds[2], inflate_zlib_checksum_fds[2], status;
	int err = 0;
	pid_t pid;
	stress_zlib_checksum_t deflate_zlib_checksum, inflate_zlib_checksum;
	ssize_t n;
	bool bad_zlib_checksum_reads = false;
	bool error = false;
	bool interrupted = false;

	(void)memset(&deflate_zlib_checksum, 0, sizeof(deflate_zlib_checksum));
	(void)memset(&inflate_zlib_checksum, 0, sizeof(inflate_zlib_checksum));

	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (pipe(fds) < 0) {
		pr_err("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	if (pipe(deflate_zlib_checksum_fds) < 0) {
		pr_err("%s: deflate zlib_checksum pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	}

	if (pipe(inflate_zlib_checksum_fds) < 0) {
		pr_err("%s: inflate zlib_checksum pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)close(deflate_zlib_checksum_fds[0]);
		(void)close(deflate_zlib_checksum_fds[1]);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)close(deflate_zlib_checksum_fds[0]);
		(void)close(deflate_zlib_checksum_fds[1]);
		(void)close(inflate_zlib_checksum_fds[0]);
		(void)close(inflate_zlib_checksum_fds[1]);

		if (!keep_stressing(args))
			return EXIT_SUCCESS;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));

		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)close(fds[1]);
		ret = stress_zlib_inflate(args, fds[0], inflate_zlib_checksum_fds[1]);
		(void)close(fds[0]);
		_exit(ret);
	} else {
		int retval;

		(void)close(fds[0]);
		ret = stress_zlib_deflate(args, fds[1], deflate_zlib_checksum_fds[1]);
		(void)close(fds[1]);
		(void)waitpid(pid, &retval, 0);
	}

	n = stress_read_buffer(deflate_zlib_checksum_fds[0], &deflate_zlib_checksum, sizeof(deflate_zlib_checksum), false);
	if (n != sizeof(deflate_zlib_checksum)) {
		bad_zlib_checksum_reads = true;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib deflate zlib_checksum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	} else {
		pipe_broken |= deflate_zlib_checksum.pipe_broken;
		interrupted |= deflate_zlib_checksum.interrupted;
		error       |= deflate_zlib_checksum.error;
	}

	n = stress_read_buffer(inflate_zlib_checksum_fds[0], &inflate_zlib_checksum, sizeof(inflate_zlib_checksum), false);
	if (n != sizeof(inflate_zlib_checksum)) {
		bad_zlib_checksum_reads = true;
		if ((errno != EINTR) && (errno != EPIPE)) {
			pr_fail("%s: zlib inflate zlib_checksum read pipe error: errno=%d (%s)\n",
				args->name, err, strerror(err));
		}
	} else {
		pipe_broken |= inflate_zlib_checksum.pipe_broken;
		interrupted |= inflate_zlib_checksum.interrupted;
		error       |= inflate_zlib_checksum.error;
	}

	if (pipe_broken || bad_zlib_checksum_reads || interrupted || error) {
		pr_inf("%s: cannot verify inflate(%d)/deflate zlib_checksums:%s%s%s%s%s%s%s\n",
			args->name,
			(int)pid,
			interrupted ? " interrupted" : "",
			(interrupted & pipe_broken) ? " and" : "",
			pipe_broken ? " broken pipe" : "",
			(((interrupted | pipe_broken)) & error) ? " and" : "",
			error ? " unexpected error" : "",
			((interrupted | pipe_broken | error) & bad_zlib_checksum_reads) ? " and" : "",
			bad_zlib_checksum_reads ? " could not read zlib_checksums" : "");
	} else {
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (deflate_zlib_checksum.zlib_checksum != inflate_zlib_checksum.zlib_checksum) {
				pr_fail("%s: zlib zlib_checksum values do NOT match "
					"%" PRIu64 "/%" PRIu64
					"(deflate/inflate)"
					" vs "
					"%" PRIu64 "/%" PRIu64
					"(deflated/inflated bytes)\n",
					args->name, deflate_zlib_checksum.zlib_checksum,
					inflate_zlib_checksum.zlib_checksum, deflate_zlib_checksum.xchars,
					inflate_zlib_checksum.xchars);
				ret = EXIT_FAILURE;
			} else {
				pr_inf("%s: zlib checksum values matches "
					"%" PRIu64"/%" PRIu64
					"(deflate/inflate)\n", args->name,
					deflate_zlib_checksum.zlib_checksum, inflate_zlib_checksum.zlib_checksum);
			}
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(deflate_zlib_checksum_fds[0]);
	(void)close(deflate_zlib_checksum_fds[1]);
	(void)close(inflate_zlib_checksum_fds[0]);
	(void)close(inflate_zlib_checksum_fds[1]);

	(void)kill(pid, SIGKILL);
	(void)shim_waitpid(pid, &status, 0);

	return ret;
}

static void stress_zlib_set_default(void)
{
	char value[21];

	(void)snprintf(value, 21, "%d", Z_BEST_COMPRESSION);
	stress_set_zlib_level(value);
	stress_set_zlib_mem_level("8");
	stress_set_zlib_method("random");
	stress_set_zlib_window_bits("15");
	stress_set_zlib_stream_bytes("0");
	(void)snprintf(value, 21, "%d", Z_DEFAULT_STRATEGY);
	stress_set_zlib_strategy(value);
}

stressor_info_t stress_zlib_info = {
	.stressor = stress_zlib,
	.set_default = stress_zlib_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_zlib_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help,
	.unimplemented_reason = "built without zlib library support"
};
#endif
